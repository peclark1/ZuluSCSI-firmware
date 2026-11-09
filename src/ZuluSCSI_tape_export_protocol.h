// v1 read-only tape-export protocol framing; host-testable, no SD/USB access.
#pragma once
#include <stddef.h>
#include <stdint.h>

// 4 KiB is an *upper bound*, not a new default. The client still requests
// 1 KiB unless explicitly configured; try larger chunks only after measuring
// SCSI-host latency and verifying the complete tape byte-for-byte.
static const size_t ZTAPE_EXPORT_MAX_CHUNK = 4096;
static const size_t ZTAPE_EXPORT_HEADER_SIZE = 18;

// The packet on USB CDC is: "ZT1D", offset(u64 LE), count(u16 LE),
// CRC32(IEEE) of data (u32 LE), followed by count raw bytes.
inline uint32_t ztapeCrc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= (uint32_t)data[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}
inline void ztapeStoreLe16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
inline void ztapeStoreLe32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (i * 8));
}
inline void ztapeStoreLe64(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8; ++i)
        p[i] = (uint8_t)(v >> (i * 8));
}
inline bool ztapeEncodeHeader(uint8_t *packet, size_t capacity,
                              uint64_t offset, size_t length, uint32_t crc)
{
    if (!packet || length > ZTAPE_EXPORT_MAX_CHUNK ||
        capacity < ZTAPE_EXPORT_HEADER_SIZE + length)
        return false;
    packet[0] = 'Z';
    packet[1] = 'T';
    packet[2] = '1';
    packet[3] = 'D';
    ztapeStoreLe64(packet + 4, offset);
    ztapeStoreLe16(packet + 12, (uint16_t)length);
    ztapeStoreLe32(packet + 14, crc);
    return true;
}
