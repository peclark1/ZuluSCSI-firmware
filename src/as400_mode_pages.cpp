// Experimental IBM 0671S15 MODE-page overlay; GPL-3.0-or-later.
#include "as400_mode_pages.h"
#include <string.h>

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}
static void put32(uint8_t *p, uint32_t x)
{
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(x >> (8 * i));
}
static uint32_t get32(const uint8_t *p)
{
    uint32_t x = 0;
    for (int i = 0; i < 4; ++i) x |= (uint32_t)p[i] << (8 * i);
    return x;
}
int as400ModeInit(As400ModePages *s, const uint8_t *raw, size_t n)
{
    if (!s || !raw || n != AS400_MODE_RAW_SIZE ||
        raw[0] != 21 || raw[3] != 8 ||
        raw[4] != 0 || raw[10] != 2 || raw[11] != 8 ||
        raw[12] != 0x81 || raw[13] != 1 ||
        raw[15] != 0x82 || raw[16] != 2 ||
        raw[19] != 0x80 || raw[20] != 1)
        return 0;
    memcpy(s->raw, raw, n);
    s->current[0] = s->saved[0] = raw[14];
    s->current[1] = s->saved[1] = raw[17];
    s->current[2] = s->saved[2] = raw[18];
    return 1;
}
int as400ModeSelect(const As400ModePages *s, int ten, int sp,
                    const uint8_t *data, size_t n, As400ModePages *next)
{
    if (!s || !data || !next) return 2;
    const size_t head = ten ? 8 : 4;
    if (n < head) return 1;
    const size_t desc = ten ? (((size_t)data[6] << 8) | data[7]) : data[3];
    if (desc > n - head) return 1;
    if (desc != 0 && desc != 8) return 2;
    // The disk image is already formatted for 520-byte sectors: *never*
    // accept a descriptor that changes capacity, density, or block size.
    if (desc && memcmp(data + head, s->raw + 4, 8)) return 2;
    if (ten) {
        if (data[0] || data[1] || data[2] || data[3] ||
            data[4] || data[5]) return 2;
    } else if (data[0] || data[1] || data[2]) return 2;

    *next = *s;
    size_t pos = head + desc;
    unsigned seen = 0;
    while (pos < n) {
        if (n - pos < 2) return 1;
        uint8_t code = data[pos];
        size_t length = data[pos + 1];
        if (length > n - pos - 2) return 1;
        // MODE SELECT page headers must have PS=0, no SPF/subpage.
        if (code & 0xc0) return 2;
        code &= 0x3f;
        unsigned bit = code == 1 ? 1u : code == 2 ? 2u : code == 0 ? 4u : 0u;
        if (!bit || (seen & bit)) return 2;
        seen |= bit;
        const uint8_t *value = data + pos + 2;
        if (code == 1 && length == 1) next->current[0] = value[0];
        else if (code == 2 && length == 2) {
            next->current[1] = value[0];
            next->current[2] = value[1];
        } else if (code == 0 && length == 1 && value[0] == s->raw[21]) {
            // Old IBM page zero: fixed, not an invitation to change a
            // vendor-specific parameter that we do not understand.
        } else return 2;
        pos += length + 2;
    }
    if (sp) memcpy(next->saved, next->current, sizeof(next->saved));
    return 0;
}
int as400ModeSense(const As400ModePages *s, int ten, int dbd, int pc,
                   int page, uint8_t *out, size_t capacity)
{
    if (!s || !out || pc < 0 || pc > 3 ||
        (page != 0x3f && page != 0 && page != 1 && page != 2))
        return -1;
    const size_t head = ten ? 8 : 4;
    const size_t desc = dbd ? 0 : 8;
    size_t total = head + desc;
    if (page == 0x3f || page == 1) total += 3;
    if (page == 0x3f || page == 2) total += 4;
    if (page == 0x3f || page == 0) total += 3;
    if (total > capacity) return -1;
    memset(out, 0, total);
    if (ten) {
        out[0] = (uint8_t)((total - 2) >> 8);
        out[1] = (uint8_t)(total - 2);
        out[2] = s->raw[1];
        out[3] = s->raw[2];
        out[7] = (uint8_t)desc;
    } else {
        out[0] = (uint8_t)(total - 1);
        out[1] = s->raw[1];
        out[2] = s->raw[2];
        out[3] = (uint8_t)desc;
    }
    if (desc) memcpy(out + head, s->raw + 4, 8);
    size_t i = head + desc;
    const uint8_t *v = pc == 3 ? s->saved : (pc == 2 ? nullptr : s->current);
    if (page == 0x3f || page == 1) {
        out[i++] = 0x81; out[i++] = 1;
        out[i++] = pc == 1 ? 0xff : pc == 2 ? s->raw[14] : v[0];
    }
    if (page == 0x3f || page == 2) {
        out[i++] = 0x82; out[i++] = 2;
        out[i++] = pc == 1 ? 0xff : pc == 2 ? s->raw[17] : v[1];
        out[i++] = pc == 1 ? 0xff : pc == 2 ? s->raw[18] : v[2];
    }
    if (page == 0x3f || page == 0) {
        out[i++] = 0x80; out[i++] = 1;
        out[i++] = pc == 1 ? 0 : s->raw[21];
    }
    return (int)i;
}
void as400ModeEncode(const As400ModePages *s, uint64_t identity,
                     uint32_t sequence, uint8_t out[AS400_MODE_RECORD_SIZE])
{
    memset(out, 0, AS400_MODE_RECORD_SIZE);
    memcpy(out, "A4MP", 4);
    out[4] = 1;
    for (int i = 0; i < 8; ++i) out[8 + i] = (uint8_t)(identity >> (8 * i));
    put32(out + 16, sequence);
    memcpy(out + 20, s->saved, 3);
    put32(out + 24, crc32(out, 24));
}
int as400ModeDecode(As400ModePages *s, uint64_t identity,
                    const uint8_t in[AS400_MODE_RECORD_SIZE], uint32_t *sequence)
{
    if (!s || !in || !sequence ||
        memcmp(in, "A4MP", 4) || in[4] != 1 ||
        in[5] || in[6] || in[7] || in[23] ||
        in[28] || in[29] || in[30] || in[31] ||
        get32(in + 24) != crc32(in, 24))
        return 0;
    for (int i = 0; i < 8; ++i)
        if (in[8 + i] != (uint8_t)(identity >> (8 * i))) return 0;
    *sequence = get32(in + 16);
    memcpy(s->saved, in + 20, 3);
    memcpy(s->current, s->saved, 3);
    return 1;
}
