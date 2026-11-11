// Flash-backed MODE SELECT/MODE SENSE overlay for direct-access disks.
// GPL-3.0-or-later. This portion is independent of any MCU or filesystem.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define MODE_PAGE_FLASH_SECTOR 4096u
#define MODE_PAGE_DATA_CAPACITY 3584u
#define MODE_PAGE_RECORD_HEADER 32u
// Each entry: identity (8), page code (1), length (1), data (up to 255).
// Reserved page bit SPF/subpages are not supported by the existing firmware.
typedef struct {
    uint16_t used;
    uint8_t bytes[MODE_PAGE_DATA_CAPACITY];
} ModePageTable;

typedef struct {
    ModePageTable current;
    ModePageTable saved;
} ModePageState;

// 0=accepted; 1=truncated MODE SELECT; 2=invalid parameter/page; 3=full.
// Changes are staged in "next" and do not affect "existing" on failure.
// baseline is a *complete*, unmodified MODE SENSE(6/10) all-pages response.
int modePageSelect(const ModePageState *existing, uint64_t identity,
                   int tenSelect, int save, const uint8_t *select, size_t len,
                   int tenBaseline, const uint8_t *baseline, size_t baselineLen,
                   ModePageState *next);
// Change only page payload bytes, not the header, identity, geometry or length.
// pc = current(0), changeable(1), default(2), saved(3).
int modePageSense(const ModePageState *state, uint64_t identity, int pc,
                  int ten, uint8_t *sense, size_t len);
// Extract an exact current/saved page payload. Returns bytes, or -1 if absent.
int modePageGet(const ModePageTable *table, uint64_t identity, int page,
                uint8_t *out, size_t capacity);
// Journal wire format v1: magic, version, sequence, used, CRC over payload.
// A whole flash erase sector holds a snapshot of all saved MODE pages.
int modePageEncodeSector(const ModePageTable *saved, uint32_t sequence,
                         uint8_t sector[MODE_PAGE_FLASH_SECTOR]);
int modePageDecodeSector(ModePageTable *saved, uint32_t *sequence,
                         const uint8_t sector[MODE_PAGE_FLASH_SECTOR]);
// -1: neither valid. Else 0..3, newest sequence modulo 32-bit wrap.
int modePageNewest(int count, const uint8_t valid[4],
                   const uint32_t sequences[4]);
// Drop transient overrides at reset: loaded saved state becomes current.
void modePageColdBoot(ModePageState *state, const ModePageTable *saved);
