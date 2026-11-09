// Native host-side tests for ZT1 binary frame format.
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../src/ZuluSCSI_tape_export_protocol.h"

int main()
{
    const uint8_t vector[] = {'1','2','3','4','5','6','7','8','9'};
    assert(ztapeCrc32(vector, sizeof(vector)) == 0xcbf43926u);
    assert(ztapeCrc32(vector, 0) == 0u);

    uint8_t packet[ZTAPE_EXPORT_HEADER_SIZE + ZTAPE_EXPORT_MAX_CHUNK] = {};
    const uint64_t offset = 0x123456789abcdef0ULL;
    assert(ztapeEncodeHeader(packet, sizeof(packet), offset, 1024, 0xcbf43926));
    assert(memcmp(packet, "ZT1D", 4) == 0);
    const uint8_t known_offset[] = {0xf0,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12};
    assert(memcmp(packet + 4, known_offset, 8) == 0);
    assert(packet[12] == 0x00 && packet[13] == 0x04);
    const uint8_t known_crc[] = {0x26,0x39,0xf4,0xcb};
    assert(memcmp(packet + 14, known_crc, 4) == 0);

    assert(!ztapeEncodeHeader(packet, sizeof(packet), 0, 1025, 0));
    assert(!ztapeEncodeHeader(packet, ZTAPE_EXPORT_HEADER_SIZE + 1, 0, 512, 0));
    assert(!ztapeEncodeHeader(nullptr, sizeof(packet), 0, 10, 0));
    assert(ztapeEncodeHeader(packet, sizeof(packet), 0, 0, 0));
    return 0;
}
