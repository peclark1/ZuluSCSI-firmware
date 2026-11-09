// Native host regression tests for the hot-path Live Monitor counters.
// Run the command in docs/live-serial-monitor.md to compile and execute.
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../src/ZuluSCSI_live_monitor.h"

int main()
{
    LiveImageIoCounters first = liveImageIoSnapshot(0);
    assert(first.readBytes == 0 && first.writeBytes == 0);
    assert(first.readCalls == 0 && first.writeCalls == 0);

    liveImageIoRecord(0, false, 512);
    liveImageIoRecord(0, true, 520);
    liveImageIoRecord(0, true, 0);
    liveImageIoRecord(0, false, -1);
    liveImageIoRecord(0xff, true, 1000);
    liveImageIoRecord(S2S_MAX_TARGETS, true, 2000);

    first = liveImageIoSnapshot(0);
    assert(first.readBytes == 512 && first.readCalls == 1);
    assert(first.writeBytes == 520 && first.writeCalls == 1);

    // The full 64-bit range must remain available for large tape images.
    liveImageIoRecord(0, true, 5LL * 1024 * 1024 * 1024);
    first = liveImageIoSnapshot(0);
    assert(first.writeBytes == 520ULL + 5ULL * 1024 * 1024 * 1024);
    assert(first.writeCalls == 2);

    liveImageIoRecord(2, false, 64);
    LiveImageIoCounters second = liveImageIoSnapshot(2);
    assert(second.readBytes == 64 && second.writeBytes == 0);
    assert(second.readCalls == 1 && second.writeCalls == 0);

    // Real write speed uses elapsed milliseconds, not a displayed cadence.
    assert(liveImageIoRateKiBPerSec(110ULL * 1024 * 5, 5000) == 110);
    assert(liveImageIoRateKiBPerSec(196ULL * 1024 * 2, 2000) == 196);
    assert(liveImageIoRateKiBPerSec(196ULL * 1024 * 2, 10000) == 39);
    assert(liveImageIoRateKiBPerSec(999, 0) == 0);

    // Small tape images / early writes should never display as 0.0MiB.
    char label[64];
    liveImageIoFormatBytes(label, sizeof(label), 0);
    assert(strcmp(label, "0B") == 0);
    liveImageIoFormatBytes(label, sizeof(label), 240);
    assert(strcmp(label, "240B") == 0);
    liveImageIoFormatBytes(label, sizeof(label), 1536);
    assert(strcmp(label, "1.5KiB") == 0);
    liveImageIoFormatBytes(label, sizeof(label), 50 * 1024);
    assert(strcmp(label, "50.0KiB") == 0);
    liveImageIoFormatBytes(label, sizeof(label), 300ULL * 1024 * 1024 + 500ULL * 1024);
    assert(strcmp(label, "300.48MiB") == 0);

    liveImageIoFormatChange(label, sizeof(label), 512, false, true);
    assert(strcmp(label, "+512B") == 0);
    liveImageIoFormatChange(label, sizeof(label), 1536, true, true);
    assert(strcmp(label, "-1.5KiB") == 0);
    liveImageIoFormatChange(label, sizeof(label), 0, false, false);
    assert(strcmp(label, "baseline") == 0);
    // All helpers safely handle zero-length output buffers.
    liveImageIoFormatBytes(label, 0, 1000);
    liveImageIoFormatChange(label, 0, 1000, false, true);

    LiveImageIoCounters invalid = liveImageIoSnapshot(S2S_MAX_TARGETS);
    assert(invalid.readBytes == 0 && invalid.writeBytes == 0);
    return 0;
}
