// Experimental opt-in saved MODE pages for profiled IBM 0671S15 disks.
// GPL-3.0-or-later. C-facing interface for SCSI2SD mode.c.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void as400ModePersistenceReset(void);
// Warm the per-device saved-state cache once after INI/profile binding.
void as400ModePersistenceWarm(uint8_t id);
// -1: feature not enabled/applicable; -2: unsupported page; >=0: full response.
int as400ModePersistenceSense(uint8_t id, int ten, int dbd, int pc,
                              int page, uint8_t *out, int capacity);
// -1: feature not enabled/applicable; 0: GOOD; 1: length;
// 2: invalid parameters; 3: durable write failure.
int as400ModePersistenceSelect(uint8_t id, int ten, int pf, int sp,
                               const uint8_t *data, int length);
#ifdef __cplusplus
}
#endif
