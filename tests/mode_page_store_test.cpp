// Host tests for the actual generic firmware MODE-page codec and journal.
#include "mode_page_store.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int ntest=0;
#define CHECK(x) do { assert(x); ++ntest; } while(0)
static const uint8_t B10_SENSE[] = {
    0x15,0,0,8, 0,9,0x67,0xc8,0,0,2,8,
    0x81,1,4, 0x82,2,1,0x80, 0x80,1,0x80
};
static const uint8_t B10_SELECT[] = {
    0,0,0,8, 0,9,0x67,0xc8,0,0,2,8,
    1,1,4, 2,2,1,0x80, 0,1,0x80
};
static size_t makeGeneric(uint8_t *buf)
{
    memset(buf,0,128);
    size_t n=16;
    buf[7]=8; buf[14]=2; buf[15]=0; // 512-byte disk
    // Pages with different lengths: recover, disconnect, format, cache.
    buf[n++]=0x81; buf[n++]=0x0a;
    for(int i=0;i<10;i++) buf[n++]=(uint8_t)(i+1);
    buf[n++]=0x82; buf[n++]=0x0e;
    for(int i=0;i<14;i++) buf[n++]=(uint8_t)(i+2);
    buf[n++]=0x83; buf[n++]=0x16;
    for(int i=0;i<22;i++) buf[n++]=0;
    buf[n-12]=2; buf[n-11]=0; // format page length 512
    buf[n++]=0x88; buf[n++]=0x0a;
    for(int i=0;i<10;i++) buf[n++]=(uint8_t)(i+4);
    buf[0]=(uint8_t)((n-2)>>8);
    buf[1]=(uint8_t)(n-2);
    return n;
}
static int newestOf(const uint8_t sectors[4][MODE_PAGE_FLASH_SECTOR],
                    ModePageTable *restored)
{
    uint8_t valid[4]={0};
    uint32_t sequence[4]={0};
    ModePageTable tmp={};
    for(int i=0;i<4;i++)
        valid[i]=(uint8_t)modePageDecodeSector(&tmp,&sequence[i],sectors[i]);
    int n=modePageNewest(4,valid,sequence);
    if(n>=0) CHECK(modePageDecodeSector(restored,&sequence[n],sectors[n]));
    return n;
}
int main()
{
    const uint64_t id5=0x555555553f3f3f3full;
    const uint64_t id6=0x666666664a4a4a4aull;
    ModePageState state={},staged={},boot={};
    uint8_t b10[sizeof(B10_SENSE)];
    memcpy(b10,B10_SENSE,sizeof(b10));
    CHECK(modePageSelect(&state,id6,0,1,B10_SELECT,sizeof(B10_SELECT),
                         0,B10_SENSE,sizeof(B10_SENSE),&staged)==0);
    CHECK(staged.saved.used>0 && state.saved.used==0);
    state=staged;
    CHECK(modePageSense(&state,id6,0,0,b10,sizeof(b10)));
    CHECK(!memcmp(b10,B10_SENSE,sizeof(b10)));
    CHECK(modePageSense(&state,id6,3,0,b10,sizeof(b10)));

    uint8_t generic[128];
    size_t gn=makeGeneric(generic);
    uint8_t select6[]={0,0,0,8, 0,0,0,0,0,0,2,0, 1,1,0x33, 2,2,0x41,0x42};
    // Fix selected 512-byte descriptor in exact SCSI block-descriptor layout.
    select6[11]=0; select6[10]=2;
    CHECK(modePageSelect(&state,id5,0,1,select6,sizeof(select6),
                         1,generic,gn,&staged)==0);
    CHECK(staged.saved.used>state.saved.used);
    state=staged;
    uint8_t cur[128];memcpy(cur,generic,gn);
    CHECK(modePageSense(&state,id5,0,1,cur,gn));
    CHECK(cur[18]==0x33 && cur[30]==0x41 && cur[31]==0x42);
    memcpy(cur,generic,gn);
    CHECK(modePageSense(&state,id5,3,1,cur,gn));
    CHECK(cur[18]==0x33 && cur[30]==0x41);
    memcpy(cur,generic,gn);
    CHECK(modePageSense(&state,id5,2,1,cur,gn));
    CHECK(!memcmp(cur,generic,gn));
    memcpy(cur,generic,gn);
    CHECK(modePageSense(&state,id5,1,1,cur,gn));
    CHECK(cur[18]==0xff && cur[30]==0xff);

    // Per-device isolation even when disk types and mode-page numbers match.
    memcpy(cur,generic,gn);
    CHECK(modePageSense(&state,id6,0,1,cur,gn));
    CHECK(!memcmp(cur,generic,gn));

    // Changing current page 01 with SP=0 must not become saved after SP=1
    // supplies only the separate page 02.
    uint8_t sel0[]={0,0,0,0, 1,1,0x75};
    CHECK(modePageSelect(&state,id5,0,0,sel0,sizeof(sel0),1,generic,gn,&staged)==0);
    CHECK(staged.current.used>0 && staged.saved.used==state.saved.used);
    state=staged;
    uint8_t sel1[]={0,0,0,0, 2,2,0x56,0x57};
    CHECK(modePageSelect(&state,id5,0,1,sel1,sizeof(sel1),1,generic,gn,&staged)==0);
    state=staged;
    memcpy(cur,generic,gn);
    CHECK(modePageSense(&state,id5,0,1,cur,gn));
    CHECK(cur[18]==0x75 && cur[30]==0x56);
    memcpy(cur,generic,gn);
    CHECK(modePageSense(&state,id5,3,1,cur,gn));
    CHECK(cur[18]==0x33 && cur[30]==0x56);

    uint8_t select10[]={
        0,0,0,0,0,0,0,8, 0,0,0,0,0,0,2,0, 8,1,0x91
    };
    CHECK(modePageSelect(&state,id5,1,1,select10,sizeof(select10),
                         1,generic,gn,&staged)==0);
    state=staged;

    // Mode page 03 remains writable only where geometry is invariant.
    uint8_t fmt[]={0,0,0,0, 3,12,0,0,0,0,0,0,0,0,0,0,2,0};
    CHECK(modePageSelect(&state,id5,0,1,fmt,sizeof(fmt),1,generic,gn,&staged)==0);
    uint8_t fmtBad[sizeof(fmt)];
    memcpy(fmtBad,fmt,sizeof(fmt));
    fmtBad[16]=4; // 1024-byte sector size, impossible without image rewrite
    CHECK(modePageSelect(&state,id5,0,1,fmtBad,sizeof(fmtBad),
                         1,generic,gn,&staged)==2);
    CHECK(state.saved.used>0); // invalid command did not commit

    uint8_t bad[sizeof(select6)];
    memcpy(bad,select6,sizeof(bad));bad[11]=1; // selected 513-byte descriptor
    CHECK(modePageSelect(&state,id5,0,1,bad,sizeof(bad),1,generic,gn,&staged)==2);
    memcpy(bad,select6,sizeof(bad));bad[16]=255;
    CHECK(modePageSelect(&state,id5,0,1,bad,sizeof(bad),1,generic,gn,&staged)==1);
    memcpy(bad,select6,sizeof(bad));bad[15]=0x3e;
    CHECK(modePageSelect(&state,id5,0,1,bad,sizeof(bad),1,generic,gn,&staged)==2);
    memcpy(bad,select6,sizeof(bad));bad[12]=0x81;
    CHECK(modePageSelect(&state,id5,0,1,bad,sizeof(bad),1,generic,gn,&staged)==2);
    CHECK(modePageSelect(&state,id5,0,1,bad,2,1,generic,gn,&staged)==1);
    CHECK(modePageSelect(&state,id5,1,1,select10,7,1,generic,gn,&staged)==1);

    // Journal tests are the *exact* firmware snapshot serializer, not Python
    // reimplementations. Faults simulate torn writes and CRC corruption.
    static uint8_t slots[4][MODE_PAGE_FLASH_SECTOR];
    for(unsigned i=0;i<4;i++) memset(slots[i],0xff,sizeof(slots[i]));
    CHECK(modePageEncodeSector(&state.saved,0xffffffffu,slots[0]));
    ModePageTable restored={};uint32_t seq=0;
    CHECK(modePageDecodeSector(&restored,&seq,slots[0]));
    CHECK(seq==0xffffffffu && restored.used==state.saved.used);
    CHECK(!memcmp(restored.bytes,state.saved.bytes,restored.used));
    modePageColdBoot(&boot,&restored);
    CHECK(boot.current.used==boot.saved.used);
    memcpy(cur,generic,gn);
    CHECK(modePageSense(&boot,id5,3,1,cur,gn));
    CHECK(cur[18]==0x33 && cur[30]==0x56);
    CHECK(newestOf(slots,&restored)==0);
    uint8_t complete[MODE_PAGE_FLASH_SECTOR];
    CHECK(modePageEncodeSector(&state.saved,0,complete));
    memcpy(slots[1],complete,MODE_PAGE_RECORD_HEADER+3); // torn in payload
    CHECK(newestOf(slots,&restored)==0);
    memcpy(slots[1],complete,sizeof(complete));
    CHECK(newestOf(slots,&restored)==1); // sequence wraps
    slots[1][MODE_PAGE_RECORD_HEADER+1]^=0x40; // corrupt newest
    CHECK(newestOf(slots,&restored)==0);
    slots[1][MODE_PAGE_RECORD_HEADER+1]^=0x40;
    CHECK(newestOf(slots,&restored)==1);
    slots[1][4]=0x7f; // future incompatible format version
    CHECK(newestOf(slots,&restored)==0);
    uint8_t noneValid[4]={0};uint32_t noneSeq[4]={0};
    CHECK(modePageNewest(4,noneValid,noneSeq)==-1);
    // Full journal, including unrelated IDs, cannot be partially committed
    // when there is no space for one more saved page.
    ModePageState filled={};
    uint8_t many[255];memset(many,0x42,sizeof(many));
    uint8_t largeBaseline[512]={0};
    // Enough entries to exercise capacity on arbitrary disk types.
    largeBaseline[3]=0;largeBaseline[4]=0x81;largeBaseline[5]=255;
    memcpy(largeBaseline+6,many,255);
    const size_t largeLen=261;
    uint8_t largeSelect[261]={0};largeSelect[4]=1;largeSelect[5]=255;
    memcpy(largeSelect+6,many,255);
    int failed=0;
    for(unsigned i=0;i<30;i++) {
        uint64_t key=10000u+i;
        int ret=modePageSelect(&filled,key,0,1,largeSelect,largeLen,
                               0,largeBaseline,largeLen,&staged);
        if(ret==3){failed=1;break;}
        CHECK(ret==0);
        filled=staged;
    }
    CHECK(failed);
    printf("PASS: %d generic MODE-page + B10 + flash-journal assertions\n",ntest);
    return 0;
}
