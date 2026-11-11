// Flash-backed current/saved MODE pages for *any* direct-access disk.
// GPL-3.0-or-later. RP2MCU: 4-sector circular snapshot journal in the
// reserved settings region. Other targets: disabled without an allocated
// flash region and safe platform read/erase/program primitives.
#include "mode_page_persistence.h"
#include "mode_page_store.h"
#include <ZuluSCSI_platform_config.h>
#include <ZuluSCSI_platform.h>
#include <string.h>

#if defined(PLATFORM_HAS_FLASH_REGION_ACCESS) && defined(PLATFORM_FLASH_SETTINGS_OFFSET) && defined(PLATFORM_FLASH_SETTINGS_SIZE)
#include <minIni.h>
#include "ZuluSCSI_config.h"
#include "ZuluSCSI_disk.h"
#include "ZuluSCSI_settings.h"
#include "ZuluSCSI_log.h"
#include "zpdb_profiles.h"
extern "C" {
#include <scsi.h>
}

static_assert(PLATFORM_FLASH_SETTINGS_SIZE == 4 * MODE_PAGE_FLASH_SECTOR,
              "persistent MODE-page journal requires exactly four erase sectors");
static_assert(PLATFORM_FLASH_SECTOR_SIZE == MODE_PAGE_FLASH_SECTOR,
              "flash sector size does not match MODE journal");
static_assert(PLATFORM_FLASH_PROGRAM_SIZE == 256,
              "flash page program granularity changed");

namespace {
static ModePageState g_pages;
static ModePageState g_staged;
// The existing 64 KiB scsiDev.data command buffer supplies scratch space
// during initialization and after MODE SELECT validation. No extra 12 KiB
// journal-sector/verification/candidate buffers are kept resident.
static bool g_started;
static bool g_enabled;
static bool g_foreign;
static bool g_allowInit;
static int g_slot = -1;
static uint32_t g_sequence;
static uint64_t g_keys[S2S_MAX_TARGETS];
static bool g_keysUsed[S2S_MAX_TARGETS];
static bool g_runtimeApplied[S2S_MAX_TARGETS];

static uint64_t fnvByte(uint64_t key, uint8_t b)
{
    return (key ^ b) * 1099511628211ull;
}
static uint64_t fnvString(uint64_t key, const char *text, size_t limit)
{
    for (size_t i=0; text && i<limit && text[i]; ++i)
        key=fnvByte(key,(uint8_t)text[i]);
    return fnvByte(key,0);
}
static uint64_t keyFor(uint8_t scsiId)
{
    // Stable across reboot and across dynamic SCA *IDs*: the path and
    // configured serial/model, not SCSI target number, identify the device.
    // An ID fallback is used only when no image filename can be obtained.
    scsiId &= S2S_CFG_TARGET_ID_BITS;
    const scsi_device_settings_t *cfg=g_scsi_settings.getDevice(scsiId);
    image_config_t &img=scsiDiskGetImageConfig(scsiId);
    uint64_t hash=14695981039346656037ull;
    hash=fnvString(hash,cfg->vendor,sizeof(cfg->vendor));
    hash=fnvString(hash,cfg->prodId,sizeof(cfg->prodId));
    hash=fnvString(hash,cfg->serial,sizeof(cfg->serial));
    const char *profile=zpdbProfileName(scsiId);
    hash=fnvString(hash,profile,64);
    static char filename[MAX_FILE_PATH + 1];
    filename[0]='\0';
    if (img.file.isOpen()) img.file.getFilename(filename,sizeof(filename));
    if (!filename[0] && img.current_image[0])
        strncpy(filename,img.current_image,sizeof(filename)-1);
    filename[sizeof(filename)-1]='\0';
    if (filename[0]) hash=fnvString(hash,filename,sizeof(filename));
    else {
        hash=fnvString(hash,"no-filename-ID",16);
        hash=fnvByte(hash,scsiId);
    }
    // Same filename reused for a different physical geometry cannot silently
    // inherit previous saved pages.
    const uint32_t blockSize=img.bytesPerSector;
    const uint32_t sectors=img.scsiSectors;
    for(unsigned i=0;i<4;i++) hash=fnvByte(hash,(uint8_t)(blockSize>>(8*i)));
    for(unsigned i=0;i<4;i++) hash=fnvByte(hash,(uint8_t)(sectors>>(8*i)));
    return hash;
}
static bool matchesDevice(uint8_t scsiId)
{
    const uint8_t id=scsiId&S2S_CFG_TARGET_ID_BITS;
    image_config_t &img=scsiDiskGetImageConfig(id);
    if (!(img.deviceType==S2S_CFG_FIXED ||
          img.deviceType==S2S_CFG_REMOVABLE ||
          img.deviceType==S2S_CFG_ZIP100 ||
          img.deviceType==S2S_CFG_MO))
        return false;
    if(!img.file.isOpen()) return false;
    return true;
}
static void applyDisconnectTiming(uint64_t key)
{
    // Keep the existing operational MODE SELECT page-02 behavior, not just
    // its readback. Standard disconnect-time-limit lives at page offsets
    // 6..7, i.e. value bytes 4..5. This is the generic 14-byte form.
    if (!scsiDev.target) return;
    uint8_t data[255];
    int n=modePageGet(&g_pages.current,key,2,data,sizeof(data));
    if(n==14) scsiDev.target->liveCfg.disconnectTimeLimit =
                ((uint16_t)data[4]<<8) | data[5];
}
static bool identity(uint8_t scsiId, uint64_t *key)
{
    const uint8_t id=scsiId&S2S_CFG_TARGET_ID_BITS;
    if(!matchesDevice(id)) return false;
    *key=keyFor(id);
    for(int i=0;i<S2S_MAX_TARGETS;i++) {
        if(i!=id && g_keysUsed[i] && g_keys[i]==*key) {
            logmsg("-- MODE flash: identical device identities on SCSI IDs ",
                   i, " and ", (int)id, " (saved MODE pages disabled)");
            return false;
        }
    }
    if(!g_keysUsed[id] || g_keys[id]!=*key)
        g_runtimeApplied[id]=false;
    g_keys[id]=*key;
    g_keysUsed[id]=true;
    if(!g_runtimeApplied[id]) {
        applyDisconnectTiming(*key);
        g_runtimeApplied[id]=true;
    }
    return true;
}
static bool readSector(unsigned slot,uint8_t *buf)
{
    return platform_flash_read(PLATFORM_FLASH_SETTINGS_OFFSET+
        slot*MODE_PAGE_FLASH_SECTOR,buf,MODE_PAGE_FLASH_SECTOR);
}
static void initialize()
{
    if(g_started) return;
    g_started=true;
    g_enabled=ini_getbool("SCSI","PersistModePages",false,CONFIGFILE);
    if(!g_enabled) return;
    g_allowInit=ini_getbool("SCSI","PersistModeFlashInitialize",false,CONFIGFILE);
    uint8_t valid[4]={0,0,0,0};
    uint32_t seq[4]={0,0,0,0};
    g_foreign=false;
    uint8_t *sector=scsiDev.data;
    for(unsigned slot=0;slot<4;slot++) {
        if(!readSector(slot,sector)) {
            logmsg("-- MODE flash: settings-sector read failure, disabling persistence");
            g_enabled=false; return;
        }
        bool erased=true;
        for(unsigned i=0;i<MODE_PAGE_FLASH_SECTOR;i++)
            if(sector[i]!=0xff) { erased=false; break; }
        if(modePageDecodeSector(&g_staged.saved,&seq[slot],sector))
            valid[slot]=1;
        else if(!erased && !modePageJournalOwned(sector)) g_foreign=true;
    }
    g_slot=modePageNewest(4,valid,seq);
    if(g_slot>=0) {
        if(!readSector((unsigned)g_slot,sector) ||
            !modePageDecodeSector(&g_staged.saved,&g_sequence,sector)) {
            g_enabled=false; return;
        }
        modePageColdBoot(&g_pages,&g_staged.saved);
        logmsg("-- MODE flash: restored saved pages; journal slot ",
               g_slot, ", sequence ", (int)g_sequence);
    } else {
        // Do not silently erase pre-existing data in a newly reserved region.
        if(g_foreign&&!g_allowInit) {
            logmsg("-- MODE flash: unfamiliar content in reserved settings area; "
                   "set PersistModeFlashInitialize=1 explicitly on a test build "
                   "to allow replacing it");
            g_enabled=false;
            return;
        }
        logmsg("-- MODE flash: no saved pages; starting with defaults");
    }
}
static bool save(const ModePageTable *next, uint8_t *sector)
{
    // The inactive slot is erased first, leaving the previous valid sector
    // intact until a new, checksummed snapshot has been verified.
    unsigned newSlot=(unsigned)(g_slot+1)&3u;
    const uint32_t seq=g_sequence+1;
    if(!modePageEncodeSector(next,seq,sector)) return false;
    const uint32_t offset=PLATFORM_FLASH_SETTINGS_OFFSET+
        newSlot*MODE_PAGE_FLASH_SECTOR;
    if(!platform_flash_erase(offset,MODE_PAGE_FLASH_SECTOR)) return false;
    if(!platform_flash_program(offset,sector,MODE_PAGE_FLASH_SECTOR))
        return false;
    if(!readSector(newSlot,sector)) return false;
    if(!modePageVerifySector(next,seq,sector)) return false;
    g_slot=(int)newSlot;
    g_sequence=seq;
    return true;
}
} // namespace
extern "C" void modePersistenceReset(void)
{
    memset(&g_pages,0,sizeof(g_pages));
    memset(&g_staged,0,sizeof(g_staged));
    memset(g_keysUsed,0,sizeof(g_keysUsed));
    memset(g_runtimeApplied,0,sizeof(g_runtimeApplied));
    g_started=false; g_enabled=false; g_foreign=false; g_allowInit=false;
    g_slot=-1; g_sequence=0;
    initialize();
}
extern "C" int modePersistenceEnabled(uint8_t id)
{
    // Initialization is done at the boot/rescan hook, never in the middle
    // of DATA OUT: it uses scsiDev.data as scratch for flash reads.
    return g_started && g_enabled && matchesDevice(id);
}
extern "C" void modePersistenceSense(uint8_t id,int pc,int ten,
                                      uint8_t *response,int length)
{
    uint64_t key;
    if(g_started && g_enabled && identity(id,&key) && length>=0)
        modePageSense(&g_pages,key,pc,ten,response,(size_t)length);
}
extern "C" int modePersistenceSelect(uint8_t id,int ten,int pf,int sp,
                                      const uint8_t *data,int len,int baseTen,
                                      uint8_t *base,int baseLen)
{
    if(!g_started || !g_enabled || !matchesDevice(id)) return -1;
    uint64_t key;
    if(!identity(id,&key)) return 3;
    if(!pf || len<0 || baseLen<0) return 2;
    int r=modePageSelect(&g_pages,key,ten,sp,data,(size_t)len,
                         baseTen,base,(size_t)baseLen,&g_staged);
    if(r) return r;
    if(sp && (g_staged.saved.used!=g_pages.saved.used ||
       memcmp(g_staged.saved.bytes,g_pages.saved.bytes,g_staged.saved.used))) {
        // The baseline has been fully consumed by modePageSelect and can
        // safely become the 4096-byte flash program/readback buffer.
        if(!save(&g_staged.saved,base)) {
            logmsg("-- MODE flash: journal save failed on ID ",(int)id);
            return 3;
        }
    }
    g_pages=g_staged;
    applyDisconnectTiming(key);
    return 0;
}
#else
extern "C" void modePersistenceReset(void) {}
extern "C" int modePersistenceEnabled(uint8_t) { return 0; }
extern "C" void modePersistenceSense(uint8_t,int,int,uint8_t*,int) {}
extern "C" int modePersistenceSelect(uint8_t,int,int,int,const uint8_t*,int,int,uint8_t*,int)
{ return -1; }
#endif
