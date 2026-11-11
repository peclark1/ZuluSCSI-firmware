// Persistent MODE page overlay for ZuluSCSI direct-access disks.
// GPL-3.0-or-later. No immutable profile, image, or INQUIRY/VPD writes.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void modePersistenceReset(void);
int modePersistenceEnabled(uint8_t scsiId);
// In-place overlay on a completed, untruncated MODE SENSE response.
void modePersistenceSense(uint8_t scsiId, int pc, int ten,
                          uint8_t *response, int length);
// -1: disabled/unsupported hardware; 0: GOOD; 1: truncated parameter list;
// 2: invalid page/parameter; 3: flash capacity/write/identity failure.
int modePersistenceSelect(uint8_t scsiId, int ten, int pf, int sp,
                          const uint8_t *data, int len,
                          int tenBaseline, const uint8_t *baseline, int baselineLen);
#ifdef __cplusplus
}
#endif
