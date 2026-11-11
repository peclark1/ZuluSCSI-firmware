// Host regression for the actual 0671S15 saved MODE-page model.
#include "as400_mode_pages.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t sense6[] = {
    0x15,0,0,8, 0,9,0x67,0xc8,0,0,2,8,
    0x81,1,4, 0x82,2,1,0x80, 0x80,1,0x80
};
// Captured IBM 9404-B10 MODE SELECT(6), CDB 15 11 00 00 16 00.
static const uint8_t cdb6[] = {0x15,0x11,0,0,0x16,0};
static const uint8_t select6[] = {
    0,0,0,8, 0,9,0x67,0xc8,0,0,2,8,
    1,1,4, 2,2,1,0x80, 0,1,0x80
};
static void equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    assert(memcmp(a,b,n) == 0);
}
int main()
{
    int count = 0;
    As400ModePages a, b, next, boot;
    uint8_t response[128], buffer[128];
    uint8_t recordA[AS400_MODE_RECORD_SIZE], recordB[AS400_MODE_RECORD_SIZE];
    uint32_t sequence = 0;
    const uint64_t keyA = 0x123456789abcdef0ull;
    const uint64_t keyB = 0xfedcba9876543210ull;
#define CHECK(x) do { assert(x); ++count; } while (0)
    CHECK(as400ModeInit(&a, sense6, sizeof(sense6)));
    CHECK(as400ModeInit(&b, sense6, sizeof(sense6)));
    CHECK(!as400ModeInit(&boot, sense6, sizeof(sense6)-1));
    CHECK(as400ModeSense(&a,0,0,0,0x3f,response,sizeof(response)) == 22);
    equal(response,sense6,22);
    ++count;
    CHECK(as400ModeSense(&a,0,0,2,0x3f,response,sizeof(response)) == 22);
    equal(response,sense6,22);
    ++count;
    CHECK(as400ModeSense(&a,0,0,3,0x3f,response,sizeof(response)) == 22);
    equal(response,sense6,22);
    ++count;
    CHECK(as400ModeSense(&a,0,0,1,0x3f,response,sizeof(response)) == 22);
    CHECK(response[14] == 0xff && response[17] == 0xff &&
          response[18] == 0xff && response[21] == 0);
    CHECK(as400ModeSense(&a,0,0,0,1,response,sizeof(response)) == 15);
    CHECK(response[12] == 0x81 && response[13] == 1 && response[14] == 4);
    CHECK(as400ModeSense(&a,0,0,0,2,response,sizeof(response)) == 16);
    CHECK(as400ModeSense(&a,0,0,0,0,response,sizeof(response)) == 15);
    CHECK(as400ModeSense(&a,0,1,0,0x3f,response,sizeof(response)) == 14);
    CHECK(as400ModeSense(&a,1,0,0,0x3f,response,sizeof(response)) == 26);
    CHECK(response[6] == 0 && response[7] == 8 && response[25] == 0x80);
    CHECK(as400ModeSense(&a,1,1,0,1,response,sizeof(response)) == 11);
    CHECK(as400ModeSense(&a,1,0,0,0x04,response,sizeof(response)) < 0);
    CHECK(as400ModeSense(&a,0,0,0,1,response,5) < 0);

    CHECK(cdb6[0] == 0x15 && (cdb6[1] & 0x11) == 0x11 && cdb6[4] == sizeof(select6));
    CHECK(as400ModeSelect(&a,0,1,select6,sizeof(select6),&next) == 0);
    CHECK(!memcmp(next.saved,a.saved,3));
    a = next;
    uint8_t changed6[sizeof(select6)];
    memcpy(changed6,select6,sizeof(changed6));
    changed6[14] = 0x0c;  // host change to page 01
    changed6[17] = 0x03;  // host change to page 02
    CHECK(as400ModeSelect(&a,0,1,changed6,sizeof(changed6),&next) == 0);
    CHECK(next.current[0] == 0x0c && next.saved[1] == 0x03);
    a = next;
    CHECK(as400ModeSense(&a,0,0,0,0x3f,response,sizeof(response)) == 22);
    CHECK(response[14] == 0x0c && response[17] == 0x03);
    CHECK(as400ModeSense(&a,0,0,2,0x3f,response,sizeof(response)) == 22);
    CHECK(response[14] == 4 && response[17] == 1);
    CHECK(as400ModeSense(&a,0,0,3,0x3f,response,sizeof(response)) == 22);
    CHECK(response[14] == 0x0c && response[17] == 3);
    // Multiple drives sharing the immutable profile must remain independent.
    CHECK(b.current[0] == 4 && b.saved[1] == 1);

    // SP=0 changes CURRENT only, even when the parameter bytes differ.
    uint8_t transient[sizeof(select6)];
    memcpy(transient, select6, sizeof(transient));
    transient[14] = 0x55;
    CHECK(as400ModeSelect(&a,0,0,transient,sizeof(transient),&next) == 0);
    CHECK(next.current[0] == 0x55 && next.saved[0] == 0x0c);
    a = next;
    CHECK(as400ModeSense(&a,0,0,0,1,response,sizeof(response)) == 15);
    CHECK(response[14] == 0x55);
    CHECK(as400ModeSense(&a,0,0,3,1,response,sizeof(response)) == 15);
    CHECK(response[14] == 0x0c);

    // SP=1 of *only page 02* must not leak the transient page 01 state.
    uint8_t page2only[] = {0,0,0,0, 2,2,7,0x80};
    CHECK(as400ModeSelect(&a,0,1,page2only,sizeof(page2only),&next) == 0);
    CHECK(next.current[0] == 0x55 && next.saved[0] == 0x0c);
    CHECK(next.current[1] == 7 && next.saved[1] == 7);
    a = next;
    uint8_t page1only[] = {0,0,0,0, 1,1,0x0d};
    CHECK(as400ModeSelect(&a,0,1,page1only,sizeof(page1only),&next) == 0);
    CHECK(next.current[0] == 0x0d && next.saved[0] == 0x0d);
    CHECK(next.current[1] == 7 && next.saved[1] == 7);
    a = next;

    // Reboot: only saved parameters survive, not SP=0 transient changes.
    as400ModeEncode(&a,keyA,7,recordA);
    CHECK(as400ModeInit(&boot,sense6,sizeof(sense6)));
    CHECK(as400ModeDecode(&boot,keyA,recordA,&sequence));
    CHECK(sequence == 7 && boot.current[0] == 0x0d && boot.saved[1] == 7);
    CHECK(!as400ModeDecode(&boot,keyB,recordA,&sequence));

    // Independent second drive with the same immutable profile.
    as400ModeEncode(&b,keyB,9,recordB);
    CHECK(as400ModeInit(&boot,sense6,sizeof(sense6)));
    CHECK(as400ModeDecode(&boot,keyB,recordB,&sequence));
    CHECK(boot.current[0] == 4 && boot.saved[1] == 1);

    // MODE SELECT(10) with exactly the captured mode pages/descriptor.
    uint8_t sel10[8+sizeof(select6)-4] = {0};
    sel10[7] = 8;
    memcpy(sel10+8,select6+4,sizeof(select6)-4);
    sel10[8+8+2] = 0x32;
    CHECK(as400ModeSelect(&b,1,1,sel10,sizeof(sel10),&next) == 0);
    CHECK(next.saved[0] == 0x32);
    CHECK(b.saved[0] == 4); // not committed before persistence completes

    // Invalid requests must not mutate anything (including on a later page).
    memcpy(buffer, select6, sizeof(select6));
    buffer[11] = 0; // disk sector size changed to 512: forbidden
    CHECK(as400ModeSelect(&b,0,1,buffer,sizeof(select6),&next) == 2);
    memcpy(buffer, select6, sizeof(select6));
    buffer[16] = 20; // second page extends beyond parameter list
    CHECK(as400ModeSelect(&b,0,1,buffer,sizeof(select6),&next) == 1);
    memcpy(buffer, select6, sizeof(select6));
    buffer[16] = 1; // known page with wrong length
    CHECK(as400ModeSelect(&b,0,1,buffer,sizeof(select6),&next) == 2);
    memcpy(buffer, select6, sizeof(select6));
    buffer[15] = 0x83; // unsupported later page
    CHECK(as400ModeSelect(&b,0,1,buffer,sizeof(select6),&next) == 2);
    memcpy(buffer, select6, sizeof(select6));
    buffer[12] = 0x81; // PS bit must be clear in MODE SELECT
    CHECK(as400ModeSelect(&b,0,1,buffer,sizeof(select6),&next) == 2);
    memcpy(buffer, select6, sizeof(select6));
    buffer[21] = 0; // unknown vendor page 00 change, forbidden
    CHECK(as400ModeSelect(&b,0,1,buffer,sizeof(select6),&next) == 2);
    memcpy(buffer, select6, sizeof(select6));
    buffer[15] = 1; // duplicate page 01
    CHECK(as400ModeSelect(&b,0,1,buffer,sizeof(select6),&next) == 2);
    CHECK(as400ModeSelect(&b,0,1,select6,3,&next) == 1);
    CHECK(as400ModeSelect(&b,1,1,sel10,7,&next) == 1);
    CHECK(b.saved[0] == 4 && b.current[1] == 1);

    // Record corruption, torn write and incompatible version: reject.
    uint8_t broken[AS400_MODE_RECORD_SIZE];
    memcpy(broken,recordA,sizeof(broken));
    broken[21] ^= 0x80;
    CHECK(!as400ModeDecode(&boot,keyA,broken,&sequence));
    memcpy(broken,recordA,sizeof(broken));
    broken[4] = 2;
    CHECK(!as400ModeDecode(&boot,keyA,broken,&sequence));
    memset(broken,0xff,sizeof(broken));
    memcpy(broken,recordA,13); // interrupted/partial inactive-slot write
    CHECK(!as400ModeDecode(&boot,keyA,broken,&sequence));
    // Active slot A still provides valid durable state after a torn B write.
    CHECK(as400ModeInit(&boot,sense6,sizeof(sense6)));
    CHECK(as400ModeDecode(&boot,keyA,recordA,&sequence));
    CHECK(boot.saved[0] == 0x0d && boot.current[0] == 0x0d);

    printf("PASS: %d IBM 0671S15 MODE SELECT/SENSE, persistence, multi-target and fault assertions\n",count);
    return 0;
}
