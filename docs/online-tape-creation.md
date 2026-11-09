# Online EMPTY tape image creation — first increment (experimental)

This change adds create-only support to the ZuluSCSI USB serial Media
Management interface on the RP2MCU platform. It does not require firmware
reboot or SD-card removal. No online disk zero-fill is implemented.

## How to use it

- From the Live Monitor, press `n` to enter Media Management, or exit the
  monitor with `q` and use the standard `my` confirmation sequence.
- Select a configured tape drive by its menu index (for the B10 this is
  usually menu index 0, SCSI ID 0).
- Press `c` for **create a new empty tape image**.
- Type a base filename such as `b10-second-dist.tap`, then Enter.
  The `.tap` extension is added if omitted.
- Press `y` to confirm. The operation queues until there have been no
  backing-image reads or writes for 3 seconds and the SCSI phase is BUS_FREE.
  Press `b` while queued to cancel. When finished, the menu displays the
  created path or a reason it did not create a file. On success, the
  console also reports the synchronous SD metadata operation duration in ms
  so we can measure potential interference with SCSI service.
- Use `l` to list images; select the newly created one and insert it with
  `i` as appropriate. The image is deliberately **not** mounted or inserted
  automatically. When done, press `b` to return through the menu; if you
  entered from the Live Monitor, it resumes automatically.

Input names contain ASCII letters, numbers, hyphen, underscore, and dot,
up to 60 characters. Spaces, path separators and traversal names are
rejected. The configured tape image directory is used; root-prefix mode
automatically adds the correct `tpN-` prefix. A filename collision is an
error—there is no overwrite, truncation or rename of existing files.

## Safety limits

- Only a configured, enabled **tape** target can use this operation.
- The create-only filesystem operation writes an empty file's FAT/exFAT
  directory entry; it does **not** touch any currently mounted image, use
  `scsiDev.data`, zero-fill a disk, eject media, or change the SCSI target.
- The 3-second check observes completed backing-image I/O, **not every SCSI
  command**. BUS_FREE and this inactivity check are conservative heuristics,
  not a guarantee that a new selection cannot occur during an SD operation.
  Directory metadata creation is synchronous. Until latency is measured on
  actual hardware, do not queue creation during a critical save/restore, disk
  maintenance, or other active high-value operation.
- Avoid rebooting or removing the SD card mid-creation. A newly created
  file is initially empty and must be selected/inserted separately.
- This is designed for *preparing* a new tape while the host remains online.
  It is not an OS/400 file-management operation and cannot independently
  change which cartridge the host currently considers loaded.
- This is the initial tape-only milestone. Online disk creation/preallocation
  is **not enabled**; the existing startup create routine's SCSI buffer and
  blocking zero-fill make it unsuitable for running alongside live SCSI I/O.

## Verification

The CI workflow runs `test/tape_create_helpers.cpp`, which checks
normalization, path/control characters, length bounds, lack of implicit
overwrite/traversal, quiet timer boundaries and timer wraparound. The firmware
build checks linked symbols and required SdFat calls. **Real hardware
latency and safety remain to be tested before using this for irreplaceable
media.**

Quick host-side test:
```sh
g++ -std=c++11 -Wall -Wextra -Werror \
    test/tape_create_helpers.cpp -o /tmp/zulu-create-tape-test
/tmp/zulu-create-tape-test
```
