// Experimental IBM 0671S15 saved MODE pages. GPL-3.0-or-later.
// Two alternating checksummed SD records; no flash or .hda writes.
#include "as400_mode_persistence.h"
#include <ZuluSCSI_platform_config.h>
#ifdef PLATFORM_AS400
#include "as400_mode_pages.h"
#include "custom_vendor_inquiry.h"
#include "zpdb_profiles.h"
#include "ZuluSCSI.h"
#include "ZuluSCSI_config.h"
#include "ZuluSCSI_log.h"
#include <scsi2sd.h>
#include <minIni.h>
#include <stdio.h>
#include <string.h>

namespace {
const char DIR[] = "/as400_mode";
struct TargetState {
    bool attempted;
    bool active;
    uint64_t identity;
    uint32_t sequence;
    int currentSlot; // -1: no previously valid record
    As400ModePages pages;
};
TargetState targets[S2S_MAX_TARGETS];

uint64_t hashIdentity(const char *profile, const uint8_t serial[8])
{
    uint64_t hash = 14695981039346656037ull;
    while (*profile) {
        hash ^= (uint8_t)*profile++;
        hash *= 1099511628211ull;
    }
    hash ^= 0;
    hash *= 1099511628211ull;
    for (unsigned i = 0; i < 8; ++i) {
        hash ^= serial[i];
        hash *= 1099511628211ull;
    }
    return hash;
}
void pathFor(uint64_t identity, int slot, char out[48])
{
    static const char hex[] = "0123456789abcdef";
    memcpy(out, DIR, sizeof(DIR) - 1);
    out[sizeof(DIR) - 1] = '/';
    const size_t start = sizeof(DIR);
    for (int i = 0; i < 16; ++i)
        out[start + i] = hex[(identity >> (60 - 4 * i)) & 15];
    out[start + 16] = slot ? 'b' : 'a';
    out[start + 17] = '\0';
}
bool readSlot(uint64_t id, int slot, As400ModePages *pages, uint32_t *sequence)
{
    char path[48];
    pathFor(id, slot, path);
    FsFile f = SD.open(path, O_RDONLY);
    if (!f) return false;
    uint8_t bytes[AS400_MODE_RECORD_SIZE];
    bool ok = (f.size() == sizeof(bytes) && f.read(bytes, sizeof(bytes)) == sizeof(bytes));
    f.close();
    return ok && as400ModeDecode(pages, id, bytes, sequence);
}
bool writeSlot(uint64_t id, int slot, const As400ModePages *pages, uint32_t seq)
{
    if (!SD.exists(DIR) && !SD.mkdir(DIR)) return false;
    char path[48];
    pathFor(id, slot, path);
    uint8_t record[AS400_MODE_RECORD_SIZE];
    as400ModeEncode(pages, id, seq, record);
    FsFile f = SD.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) return false;
    bool ok = f.write(record, sizeof(record)) == sizeof(record);
    if (ok) ok = f.sync();
    f.close();
    if (!ok) return false;
    // Do not report GOOD unless the freshly stored record can be read back
    // and passes its identity/CRC validation.
    As400ModePages reread = *pages;
    uint32_t verifySeq = 0;
    return readSlot(id, slot, &reread, &verifySeq) &&
           verifySeq == seq && !memcmp(reread.saved, pages->saved, 3);
}
bool initialize(uint8_t scsiId)
{
    const uint8_t id = scsiId & S2S_CFG_TARGET_ID_BITS;
    TargetState &t = targets[id];
    if (t.attempted) return t.active;
    t.attempted = true;
    t.currentSlot = -1;
    if (!ini_getbool("SCSI", "AS400_PersistModePages", false, CONFIGFILE))
        return false;

    uint32_t blockSize = 0, sectors = 0;
    if (!getAS400ProfileCapacity(id, &blockSize, &sectors) || blockSize != 520)
        return false;
    const char *profile = zpdbProfileName(id);
    if (!profile || strcmp(profile, "IBM_0671S15_200")) return false;
    uint8_t serial[8];
    // Require a configured, stable, unique serial. No SCSI ID is part of
    // the key; a dynamic SCA ID can move without losing the saved pages.
    if (!getAS400DiskSerialOverride(id, serial)) {
        logmsg("-- AS400 MODE: SCSI ID ", (int)id,
               " missing AS400_DiskSerialNumber; persistence disabled");
        return false;
    }
    t.identity = hashIdentity(profile, serial);
    for (int i = 0; i < S2S_MAX_TARGETS; ++i) {
        if (i != id && targets[i].active &&
            targets[i].identity == t.identity) {
            logmsg("-- AS400 MODE: duplicate profile/serial identity on IDs ",
                   i, " and ", (int)id, "; disabling persistence on both");
            targets[i].active = false;
            return false;
        }
    }
    uint8_t raw[512];
    uint32_t len = 0;
    if (!zpdbReadModeSense(id, raw, sizeof(raw), &len) ||
        !as400ModeInit(&t.pages, raw, len)) {
        logmsg("-- AS400 MODE: unsupported captured MODE layout on ID ", (int)id);
        return false;
    }
    t.active = true;
    As400ModePages a = t.pages, b = t.pages;
    uint32_t seqA = 0, seqB = 0;
    bool validA = readSlot(t.identity, 0, &a, &seqA);
    bool validB = readSlot(t.identity, 1, &b, &seqB);
    if (validA && (!validB || (int32_t)(seqA - seqB) > 0)) {
        t.pages = a; t.sequence = seqA; t.currentSlot = 0;
    } else if (validB) {
        t.pages = b; t.sequence = seqB; t.currentSlot = 1;
    }
    logmsg("-- AS400 MODE: SCSI ID ", (int)id,
           " persistent mode pages enabled; saved record slot ",
           t.currentSlot, ", sequence ", (int)t.sequence);
    return true;
}
} // namespace

extern "C" void as400ModePersistenceReset(void)
{
    memset(targets, 0, sizeof(targets));
}
extern "C" int as400ModePersistenceSense(uint8_t id, int ten, int dbd,
                                          int pc, int page, uint8_t *out, int cap)
{
    id &= S2S_CFG_TARGET_ID_BITS;
    if (!initialize(id)) return -1;
    const int n = as400ModeSense(&targets[id].pages, ten, dbd, pc, page,
                                out, cap > 0 ? (size_t)cap : 0);
    return n < 0 ? -2 : n;
}
extern "C" int as400ModePersistenceSelect(uint8_t id, int ten, int pf, int sp,
                                           const uint8_t *data, int len)
{
    id &= S2S_CFG_TARGET_ID_BITS;
    if (!initialize(id)) return -1;
    if (!pf) return 2;
    TargetState &t = targets[id];
    As400ModePages next;
    int result = as400ModeSelect(&t.pages, ten, sp, data,
                                 len >= 0 ? (size_t)len : 0, &next);
    if (result) return result;
    if (sp && memcmp(next.saved, t.pages.saved, sizeof(next.saved))) {
        const int slot = t.currentSlot == 0 ? 1 : 0;
        uint32_t seq = t.sequence + 1;
        if (!writeSlot(t.identity, slot, &next, seq)) {
            logmsg("-- AS400 MODE: persistent write/verification failed, ID ", (int)id);
            return 3; // do not acknowledge a save we could not complete
        }
        t.currentSlot = slot;
        t.sequence = seq;
    }
    t.pages = next;
    return 0;
}
#else
extern "C" void as400ModePersistenceReset(void) {}
extern "C" int as400ModePersistenceSense(uint8_t, int, int, int, int, uint8_t*, int) { return -1; }
extern "C" int as400ModePersistenceSelect(uint8_t, int, int, int, const uint8_t*, int) { return -1; }
#endif
