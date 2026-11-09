# ZuluSCSI live serial monitor (experimental)

This is a read-only diagnostic view for the RP2MCU USB serial console.
It does not change SCSI device configuration or image contents.

## Usage

1. Connect to the ZuluSCSI USB serial console as usual.
2. Type `v` once to enter the monitor immediately. It is read-only and
   needs no confirmation. (Earlier test builds required `vy`.)
3. The first snapshot appears immediately. Further snapshots arrive every
   five seconds. Type `0` for a ten-second interval or `5` to restore five.
   Changing cadence schedules the *next* sample after the new interval;
   it does not force a misleading short sample. Type `r` to refresh
   immediately (this can produce a shorter actual measurement window),
   or `q` to exit. On experimental branches with online tape creation,
   press `n` for Media Management; the monitor pauses, and resumes when
   you exit the media menu.
4. The existing `m` menu remains available separately to enumerate removable
   media images and their sizes. The monitor only displays **open images**.

The first snapshot establishes the file-size baseline (shown as `baseline`).
Subsequent snapshots show changes in the current image's logical size,
including reductions if a tape image is truncated or rewritten. Switching to a
new image resets the size-change baseline for that SCSI target.
Small files and deltas are shown in bytes or KiB rather than rounded to
`0.0MiB`; larger values use two decimal places of MiB.

## Meaning of the numbers

- `R`, `W`: completed **backing-image** read/write KiB/s during the
  actual period displayed as `dt` on each target row. This matters when
  the user requests an immediate refresh or the SCSI bus is busy. The
  headline indicates the configured refresh cadence, not a guaranteed
  measurement window. These are not SCSI bus transfer rates, so
  tape framing, image-format headers, prefetch and other image accesses can
  contribute; cache hits may omit backing reads. There is one counter set
  per SCSI target ID, cumulative since firmware boot.
- `calls R/W`: number of successful ImageBackingStore read/write calls in
  the interval, not SCSI CDB counts or logical tape record counts.
- `size`: logical size reported by the currently open image. For file-based
  tape images this can grow as written; preallocated disk images ordinarily
  do not grow. RAW and gapped/partition-backed targets may report logical
  capacity rather than a filesystem file length.
- `delta:+/-`: size change since the previous snapshot of that device.
  The first observation of each loaded image is labeled `baseline`.
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
- The original feature was exercised successfully on the IBM 9404-B10
  running OS/400 V2R1: B-mode IPL completed to sign-on, and a distribution
  tape was created with simultaneous image reads/writes and visible .tap
  growth. This refined display/precision revision still needs a separate
  hardware re-test. These observations do not establish a performance
  equivalence benchmark or universal compatibility.
- Other console/log output may interleave with the screen while debugging.

Native counter test:
```sh
g++ -std=c++11 -Wall -Wextra -Werror -Itest/stubs \
    test/live_monitor_counters.cpp -o /tmp/zulu-live-test
/tmp/zulu-live-test
```
