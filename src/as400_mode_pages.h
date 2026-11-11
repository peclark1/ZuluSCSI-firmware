// Experimental IBM 0671S15 MODE-page model (GPL-3.0-or-later).
// No I/O, SCSI runtime dependencies, or mutable ZPDB data.
#pragma once
#include <stdint.h>
#include <stddef.h>
#define AS400_MODE_RAW_SIZE 22
#define AS400_MODE_RECORD_SIZE 32
typedef struct {
    uint8_t raw[AS400_MODE_RAW_SIZE];
    uint8_t current[3]; // pages 01 (1 byte), 02 (2 bytes)
    uint8_t saved[3];
} As400ModePages;

// Restrict the model to the verified 0671S15 three-short-page capture.
int as400ModeInit(As400ModePages *s, const uint8_t *raw, size_t n);
// 0=success, 1=truncated list, 2=invalid/unsupported parameters.
int as400ModeSelect(const As400ModePages *s, int ten, int sp,
                    const uint8_t *data, size_t n, As400ModePages *next);
// MODE SENSE(6/10): complete result length, or -1 for unsupported page/PC.
int as400ModeSense(const As400ModePages *s, int ten, int dbd, int pc,
                   int page, uint8_t *out, size_t capacity);
// Selects newest valid journal slot: -1=none, 0=A, 1=B.
// Handles wrapped 32-bit sequence numbers under the two-slot invariant.
int as400ModePreferredSlot(int validA, uint32_t seqA,
                           int validB, uint32_t seqB);
// Fixed endian-independent record with CRC32; never writes any backing image.
void as400ModeEncode(const As400ModePages *s, uint64_t identity,
                     uint32_t sequence, uint8_t out[AS400_MODE_RECORD_SIZE]);
int as400ModeDecode(As400ModePages *s, uint64_t identity,
                    const uint8_t in[AS400_MODE_RECORD_SIZE], uint32_t *sequence);
