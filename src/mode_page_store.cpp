// Generic direct-access MODE-page model and flash journal. GPL-3.0-or-later.
#include "mode_page_store.h"
#include <string.h>

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t v = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
        v ^= p[i];
        for (int b = 0; b < 8; ++b)
            v = (v >> 1) ^ ((v & 1) ? 0xedb88320u : 0);
    }
    return ~v;
}
static void set32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i*8));
}
static uint32_t get32(const uint8_t *p) {
    uint32_t v=0; for(unsigned i=0;i<4;++i) v|=(uint32_t)p[i] << (i*8);
    return v;
}
static unsigned entrySize(const uint8_t *p) { return 10u + p[9]; }
static uint64_t entryKey(const uint8_t *p) {
    uint64_t v = 0; for(unsigned i=0;i<8;++i) v|=(uint64_t)p[i] << (i*8);
    return v;
}
static const uint8_t *findEntry(const ModePageTable *t, uint64_t key, int page)
{
    unsigned i=0;
    while (i < t->used) {
        const uint8_t *p=t->bytes+i;
        const unsigned n=entrySize(p);
        if (n > t->used-i) return nullptr;
        if (entryKey(p)==key && (p[8]&0x3f)==page) return p;
        i+=n;
    }
    return nullptr;
}
static int putEntry(ModePageTable *t, uint64_t key, int page,
                    const uint8_t *value, unsigned count)
{
    if (count > 255 || page < 0 || page > 0x3f) return 2;
    unsigned at=0;
    while (at < t->used) {
        const uint8_t *p=t->bytes+at;
        unsigned n=entrySize(p);
        if (n > t->used-at) return 2;
        if (entryKey(p)==key && (p[8]&0x3f)==page) {
            if (p[9]==count && !memcmp(p+10,value,count))
                return 0; // identical SP=1 must not wear flash or reorder state
            // Leave other device/page entries intact.
            memmove(t->bytes+at,t->bytes+at+n,t->used-at-n);
            t->used-=n;
            break;
        }
        at+=n;
    }
    if (t->used + 10u + count > MODE_PAGE_DATA_CAPACITY) return 3;
    uint8_t *p=t->bytes+t->used;
    for (unsigned b=0;b<8;++b) p[b]=(uint8_t)(key>>(b*8));
    p[8]=(uint8_t)page; p[9]=(uint8_t)count;
    memcpy(p+10,value,count);
    t->used+=(uint16_t)(10u+count);
    return 0;
}
static int parseSenseStart(int ten, const uint8_t *data, size_t len,
                           size_t *start)
{
    unsigned hdr=ten?8:4;
    if (len < hdr) return 0;
    size_t desc=ten?((unsigned)data[6]<<8)|data[7]:data[3];
    if (desc > len-hdr) return 0;
    *start=hdr+desc;
    return 1;
}
static int baselinePage(int ten, const uint8_t *data, size_t len, int page,
                        const uint8_t **value, unsigned *valueLen)
{
    size_t pos;
    if(!parseSenseStart(ten,data,len,&pos)) return 0;
    while(pos < len) {
        if (len-pos<2) return 0;
        unsigned n=data[pos+1];
        if (n>len-pos-2 || (data[pos]&0x40)) return 0;
        if ((data[pos]&0x3f)==page) {
            *value=data+pos+2; *valueLen=n; return 1;
        }
        pos+=2+n;
    }
    return 0;
}
int modePageSelect(const ModePageState *old, uint64_t identity,
                   int tenSelect, int save, const uint8_t *input, size_t len,
                   int tenBaseline, const uint8_t *baseline, size_t baselineLen,
                   ModePageState *next)
{
    if (!old || !input || !baseline || !next) return 2;
    unsigned head=tenSelect?8:4;
    if(len<head) return 1;
    unsigned desc=tenSelect?((unsigned)input[6]<<8)|input[7]:input[3];
    if (desc>len-head) return 1;
    if (desc!=0 && desc!=8) return 2;
    // For a direct-access device, keep the backing image geometry invariant.
    // The host may specify number-of-blocks/density, but it may not silently
    // reinterpret an existing disk's sector size in MODE SELECT.
    if (desc) {
        size_t s;
        if (!parseSenseStart(tenBaseline,baseline,baselineLen,&s) ||
            s<(tenBaseline?8u:4u)+8u) return 2;
        const size_t bhead=tenBaseline?8:4;
        if(memcmp(input+head+5,baseline+bhead+5,3)) return 2;
    }
    if (tenSelect) {
        if(input[0]||input[1]||input[2]||input[3]||input[4]||input[5])
            return 2;
    } else if(input[0]||input[1]||input[2]) return 2;

    *next=*old;
    size_t pos=head+desc;
    uint64_t seen=0;
    while(pos<len) {
        if(len-pos<2) return 1;
        unsigned code=input[pos];
        unsigned n=input[pos+1];
        if(n>len-pos-2) return 1;
        if(code&0xc0) return 2; // PS/SPF prohibited in SELECT short-format pages
        code&=0x3f;
        if(seen & (1ull<<code)) return 2;
        seen |= 1ull<<code;
        const uint8_t *base=nullptr;
        unsigned baseN=0;
        if(!baselinePage(tenBaseline,baseline,baselineLen,(int)code,&base,&baseN))
            return 2;
        // Older initiators (including the B10) may write only a leading
        // portion of a captured, longer MODE SENSE page.
        if(n>baseN) return 2;
        uint8_t full[256];
        memcpy(full,base,baseN);
        const uint8_t *current=findEntry(&next->current,identity,code);
        if(current && current[9]==baseN) memcpy(full,current+10,baseN);
        memcpy(full,input+pos+2,n);
        if(code==3 && baseN>=12 &&
           (full[10]!=base[10] || full[11]!=base[11]))
            return 2; // FORMAT DEVICE page sector size remains invariant
        int r=putEntry(&next->current,identity,code,full,baseN);
        if(r) return r;
        if(save) {
            // SP=1 only saves the explicitly supplied page, never unrelated
            // transient SP=0 changes on other pages.
            r=putEntry(&next->saved,identity,code,full,baseN);
            if(r) return r;
        }
        pos+=2+n;
    }
    return 0;
}
int modePageSense(const ModePageState *state, uint64_t identity, int pc,
                  int ten, uint8_t *sense, size_t len)
{
    if(!state||!sense||pc<0||pc>3) return 0;
    if(pc==2) return 1; // immutable baseline; never overlay defaults.
    size_t pos;
    if(!parseSenseStart(ten,sense,len,&pos)) return 0;
    while(pos<len) {
        if(len-pos<2) return 0;
        unsigned n=sense[pos+1];
        if(n>len-pos-2 || (sense[pos]&0x40)) return 0;
        const uint8_t *value=nullptr;
        if(pc==0) value=findEntry(&state->current,identity,sense[pos]&0x3f);
        if(pc==3) value=findEntry(&state->saved,identity,sense[pos]&0x3f);
        if(pc==1) {
            // Report page data as changeable on the opt-in metadata overlay,
            // with the format-device sector length still read-only.
            memset(sense+pos+2,0xff,n);
            if((sense[pos]&0x3f)==3 && n>=12) {
                sense[pos+12]=0;
                sense[pos+13]=0;
            }
        } else if(value && value[9]==n) {
            memcpy(sense+pos+2,value+10,n);
        }
        pos+=2+n;
    }
    return 1;
}
int modePageGet(const ModePageTable *table,uint64_t identity,int page,
                uint8_t *out,size_t capacity)
{
    if(!table||!out) return -1;
    const uint8_t *p=findEntry(table,identity,page);
    if(!p || p[9]>capacity) return -1;
    memcpy(out,p+10,p[9]);
    return p[9];
}
int modePageEncodeSector(const ModePageTable *saved, uint32_t sequence,
                         uint8_t sector[MODE_PAGE_FLASH_SECTOR])
{
    if(!saved||!sector||saved->used>MODE_PAGE_DATA_CAPACITY) return 0;
    memset(sector,0xff,MODE_PAGE_FLASH_SECTOR);
    memcpy(sector,"ZMPJ",4);
    sector[4]=1; sector[5]=0;
    sector[6]=(uint8_t)saved->used;
    sector[7]=(uint8_t)(saved->used>>8);
    set32(sector+8,sequence);
    memcpy(sector+MODE_PAGE_RECORD_HEADER,saved->bytes,saved->used);
    set32(sector+12,crc32(sector+MODE_PAGE_RECORD_HEADER,saved->used));
    set32(sector+16,crc32(sector,16)); // header check
    return 1;
}
int modePageDecodeSector(ModePageTable *saved, uint32_t *sequence,
                         const uint8_t sector[MODE_PAGE_FLASH_SECTOR])
{
    if(!saved||!sequence||!sector||memcmp(sector,"ZMPJ",4)||
       sector[4]!=1||sector[5]||sector[20]!=0xff||sector[31]!=0xff)
        return 0;
    unsigned n=(unsigned)sector[6]|((unsigned)sector[7]<<8);
    if(n>MODE_PAGE_DATA_CAPACITY||
       get32(sector+16)!=crc32(sector,16)||
       get32(sector+12)!=crc32(sector+MODE_PAGE_RECORD_HEADER,n))
        return 0;
    // Reject a valid-CRC but malformed/duplicated payload before trusting it.
    size_t pos=0;
    while(pos<n) {
        if(n-pos<10) return 0;
        const uint8_t *p=sector+MODE_PAGE_RECORD_HEADER+pos;
        unsigned step=entrySize(p);
        if(step>n-pos || (p[8]&0xc0)) return 0;
        // No arbitrary maximum number of pages: validate duplicates by
        // walking the already-validated prefix of this compact snapshot.
        size_t earlier=0;
        while(earlier<pos) {
            const uint8_t *q=sector+MODE_PAGE_RECORD_HEADER+earlier;
            if(entryKey(q)==entryKey(p) && q[8]==p[8]) return 0;
            earlier+=entrySize(q);
        }
        pos+=step;
    }
    saved->used=(uint16_t)n;
    memcpy(saved->bytes,sector+MODE_PAGE_RECORD_HEADER,n);
    *sequence=get32(sector+8);
    return 1;
}
int modePageVerifySector(const ModePageTable *expected,uint32_t sequence,
                         const uint8_t sector[MODE_PAGE_FLASH_SECTOR])
{
    if(!expected||!sector||expected->used>MODE_PAGE_DATA_CAPACITY)
        return 0;
    unsigned n=(unsigned)sector[6]|((unsigned)sector[7]<<8);
    return !memcmp(sector,"ZMPJ",4) && sector[4]==1 && sector[5]==0 &&
           sector[20]==0xff && sector[31]==0xff &&
           n==expected->used && get32(sector+8)==sequence &&
           get32(sector+16)==crc32(sector,16) &&
           get32(sector+12)==crc32(sector+MODE_PAGE_RECORD_HEADER,n) &&
           !memcmp(sector+MODE_PAGE_RECORD_HEADER,expected->bytes,n);
}
int modePageJournalOwned(const uint8_t sector[MODE_PAGE_FLASH_SECTOR])
{
    if(!sector) return 0;
    static const uint8_t magic[4]={'Z','M','P','J'};
    int matched=0;
    while(matched<4 && sector[matched]==magic[matched]) ++matched;
    if(matched==4) return 1;
    // First-time erase followed by a power interruption in the first
    // program page may leave only a prefix of the four magic bytes.
    if(matched==0 || sector[matched]!=0xff) return 0;
    for(unsigned i=(unsigned)matched;i<MODE_PAGE_FLASH_SECTOR;i++)
        if(sector[i]!=0xff) return 0;
    return 1;
}
int modePageNewest(int count,const uint8_t valid[4],const uint32_t seq[4])
{
    if(!valid||!seq||count<0||count>4) return -1;
    int best=-1;
    for(int i=0;i<count;i++) if(valid[i]) {
        if(best<0) best=i;
        else {
            uint32_t delta=seq[i]-seq[best];
            if(delta && delta<0x80000000u) best=i;
        }
    }
    return best;
}
void modePageColdBoot(ModePageState *s,const ModePageTable *saved)
{
    if(!s||!saved) return;
    s->saved=*saved;
    s->current=*saved;
}
