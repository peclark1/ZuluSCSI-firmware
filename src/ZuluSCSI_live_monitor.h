// ZuluSCSI Live Monitor: inexpensive per-device backing-image I/O counters.
//
// These are successful ImageBackingStore bytes, NOT SCSI bus bytes. They include
// tape framing, media headers, prefetch and other image accesses, and exclude
// filesystem directory enumeration. Counters are cumulative since boot.
//
// Accesses are made from ZuluSCSI's main SCSI-service loop. No SD I/O, dynamic
// allocation, serial printing or timing calls occur in the transfer path.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <scsi2sd.h>

struct LiveImageIoCounters
{
    uint64_t readBytes;
    uint64_t writeBytes;
    uint64_t readCalls;
    uint64_t writeCalls;
};

inline LiveImageIoCounters *liveImageIoCounters()
{
    static LiveImageIoCounters counters[S2S_MAX_TARGETS] = {};
    return counters;
}

inline void liveImageIoRecord(uint8_t scsiId, bool isWrite, int64_t completedBytes)
{
    if (scsiId >= S2S_MAX_TARGETS || completedBytes <= 0)
        return;

    LiveImageIoCounters &c = liveImageIoCounters()[scsiId];
    if (isWrite)
    {
        c.writeBytes += (uint64_t)completedBytes;
        ++c.writeCalls;
    }
    else
    {
        c.readBytes += (uint64_t)completedBytes;
        ++c.readCalls;
    }
}

inline LiveImageIoCounters liveImageIoSnapshot(uint8_t scsiId)
{
    LiveImageIoCounters empty = {};
    return scsiId < S2S_MAX_TARGETS ? liveImageIoCounters()[scsiId] : empty;
}

// Use the actual sampling time, not the configured display interval. A
// manual refresh, SCSI-busy delay or interval change can shorten/lengthen
// the measurement window. All arithmetic is integer-only for MCU targets.
inline uint64_t liveImageIoRateKiBPerSec(uint64_t completedBytes, uint32_t elapsedMs)
{
    if (elapsedMs == 0)
        return 0;
    return (completedBytes * 1000ULL) / (uint64_t(elapsedMs) * 1024ULL);
}

// Human-readable quantities with enough resolution for small SIMH tape
// images and sub-MiB file growth. No allocation, no floats, no SD access.
// The format is deliberately reusable by native host-side tests.
inline void liveImageIoFormatBytes(char *out, size_t capacity, uint64_t bytes)
{
    if (!out || capacity == 0)
        return;
    if (bytes >= 1048576ULL)
        snprintf(out, capacity, "%llu.%02lluMiB",
                 (unsigned long long)(bytes / 1048576ULL),
                 (unsigned long long)((bytes % 1048576ULL) * 100ULL / 1048576ULL));
    else if (bytes >= 1024ULL)
        snprintf(out, capacity, "%llu.%01lluKiB",
                 (unsigned long long)(bytes / 1024ULL),
                 (unsigned long long)((bytes % 1024ULL) * 10ULL / 1024ULL));
    else
        snprintf(out, capacity, "%lluB", (unsigned long long)bytes);
}

inline void liveImageIoFormatChange(char *out, size_t capacity,
                                    uint64_t delta, bool shrank, bool haveBaseline)
{
    if (!out || capacity == 0)
        return;
    if (!haveBaseline)
    {
        snprintf(out, capacity, "baseline");
        return;
    }
    char quantity[48];
    liveImageIoFormatBytes(quantity, sizeof(quantity), delta);
    snprintf(out, capacity, "%c%s", shrank ? '-' : '+', quantity);
}

// Read current open-image metadata without enumerating directories or touching
// SCSI media state. Implemented in ZuluSCSI_disk.cpp to keep disk-specific
// headers and SdFs linkage out of the serial platform module. Call only when
// the SCSI bus is idle; returns false if the target has no open image.
bool liveImageIoGetStatus(uint8_t scsiId, char *name, size_t nameCapacity,
                          uint64_t *logicalSize, uint8_t *deviceType);
