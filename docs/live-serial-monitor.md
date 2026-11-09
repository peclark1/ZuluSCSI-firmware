# ZuluSCSI live serial monitor (experimental)

This is a read-only diagnostic view for the RP2MCU USB serial console.
It does not change SCSI device configuration or image contents.

## Usage

1. Connect to the ZuluSCSI USB serial console as usual.
2. Type `v` and then `y` to enter the monitor.
3. The first snapshot appears immediately. Further snapshots arrive every
   five seconds. Type `0` for a ten-second interval or `5` to restore five.
   Type `r` to refresh immediately and `q` to exit.
4. The existing `m` menu remains available separately to enumerate removable
   media images and their sizes. The monitor only displays **open images**.

The first snapshot establishes the file-size baseline, so growth is zero.
Subsequent snapshots show changes in the current image's logical size,
including reductions if a tape image is truncated or rewritten. Switching to a
new image resets the size-change baseline for that SCSI target.

## Meaning of the numbers

- `R`, `W`: completed **backing-image** read/write KiB/s during the
  preceding sampling period. These are not SCSI bus transfer rates, so
  tape framing, image-format headers, prefetch and other image accesses can
  contribute; cache hits may omit backing reads. There is one counter set
  per SCSI target ID, cumulative since firmware boot.
- `calls R/W`: number of successful ImageBackingStore read/write calls in
  the interval, not SCSI CDB counts or logical tape record counts.
- `size`: logical size reported by the currently open image. For file-based
  tape images this can grow as written; preallocated disk images ordinarily
  do not grow. RAW and gapped/partition-backed targets may report logical
  capacity rather than a filesystem file length.
- `+/-`: size change since the previous snapshot of that device.
- Name: currently open image's basename, or a raw/partition fallback.

## Safety and performance

- Counters update in memory after successful reads/writes. They do not
  perform serial writes, read the SD directory, allocate memory, or create
  logs in the hot I/O path.
- Each console poll sends at most one bounded USB CDC fragment, **only while
  the SCSI bus is free**. A long uninterrupted SCSI transfer can therefore
  delay the screen update until the command finishes.
- Refresh is not a filesystem synchronization or durability guarantee;
  the image-size display does not flush, truncate or close any file.
- This monitor has not yet been hardware-validated on the IBM 9404 B10.
  Verify no throughput regression under long disk reads and sustained tape
  writes before enabling it for preservation work.
- Other console/log output may interleave with the screen while debugging.

Native counter test:
```sh
g++ -std=c++11 -Wall -Wextra -Werror -Itest/stubs \
    test/live_monitor_counters.cpp -o /tmp/zulu-live-test
/tmp/zulu-live-test
```
