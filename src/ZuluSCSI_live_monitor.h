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
