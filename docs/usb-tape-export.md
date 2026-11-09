# ZuluSCSI USB tape export (experimental, ZT1)

## Goal

Download a **completed, closed, unmounted** SIMH `.tap` image to a Linux
computer while the ZuluSCSI continues running as the AS/400's SCSI target.
The source is never modified, deleted, truncated, or attached as USB mass
storage. The AS/400 does not need to shut down or IPL again.

**This is a draft feature, not yet hardware-qualified.** Prefer to test with
disposable images on an idle system. Do not flash new firmware during a save.

## Workflow

1. Finish the AS/400 tape operation and unload it, preferably through OS/400.
2. Make sure the completed image is **no longer open** in ZuluSCSI. Some
   unload sequences leave the old tape handle open; a drive that reports
   ejected is **not** necessarily safe for export. Select another known blank
   tape using Media Management if necessary to close the old handle.
3. Disconnect any interactive serial terminal from the Blaster's USB CDC
   port; the download utility requires exclusive serial access.
4. On Ubuntu, install pyserial (for Ubuntu apt setups: `sudo apt install
   python3-serial`).
5. Run (the output path is an existing destination directory):

   ```sh
   python3 utils/zuluscsi_tape_fetch.py \
       --port /dev/ttyACM0 \
       --scsi-id 0 \
       --image v2r1-dist-tape-created-on-b10.tap \
       --output ~/tape-archives/V2R1/
   ```

6. The downloader requests `f` from the ordinary serial console or active
   Live Monitor, opens only the specified tape image for reading, and
   requests blocks while idle. On completion it computes SHA-256 locally,
   atomically exposes the file with its final name, and leaves the source
   image on the card intact.
7. If interrupted, rerun the same command with `--resume` after the source
   is confirmed unchanged. The downloader stores `.part` and
   `.part.json` alongside the final path. Resume checks the reported
   size, metadata, and the first/last block of the previously transferred
   region. This is not a full cryptographic proof of source immutability.

## ZT1 protocol

The `f` console key enters a dedicated USB serial transfer session and
temporarily stops the Live Monitor. USB log emission is suppressed while
the protocol is active; internal logging continues. Once the session ends,
console logging and (when applicable) the Live Monitor resume. The protocol
is **stop-and-wait**; do not pipeline commands.

- `f` (single byte, no newline) -> `ZT1 READY\n`
- `OPEN <scsi-id> <tape-name.tap>\n` -> `ZT1 SIZE <size-bytes>\n` or
  `ZT1 ERR <code>\n`. Filenames must be simple ASCII basenames; no paths
  or spaces, no arbitrary SD-card read access. The image is opened
  read-only from the configured tape directory.
- `READ <byte-offset> <count>\n` (1..1024) -> binary frame:
  `ZT1D` (4 bytes), uint64 little-endian offset, uint16 little-endian
  count, uint32 little-endian CRC32/IEEE of payload, followed by exactly
  count bytes. Errors respond `ZT1 ERR <code>\n` instead.
- `CLOSE\n` -> `ZT1 CLOSED\n`
- `QUIT\n` -> `ZT1 BYE\n`
- `PING\n` -> `ZT1 PONG\n`

Each chunk is CRC-checked by the Ubuntu host before being saved. The host
computes a **SHA-256 of the completed local copy**. There is no whole-source
SHA-256 pass in firmware (which would cause extra SD I/O). Thus this is
transport integrity and conditional file-stability checking, **not an
independent end-to-end cryptographic comparison with the source SD file**.
For preservation, retain the source until independent verification.

## Operational safety

- Transfer only from a configured tape image directory, matching a
  `.tap` basename returned by Media Management's file enumerator.
- Reject images with a matching name still open on **any** SCSI target,
  even if marked ejected. Check again on every READ and abort if the
  source length changes. This is deliberately conservative.
- Read at most 1024 bytes per request, with a separate fixed buffer.
  Do not use `scsiDev.data` and do not access an active image's file
  pointer.
- File open/seek/read and USB packet emission run only when the main loop
  sees `BUS_FREE`; file I/O additionally requires 20 ms with no
  successful backing-image bytes on any target. Progress may stop under
  sustained SCSI traffic; that is preferable to disrupting the AS/400.
- **No hard real-time guarantee:** even a single SD `open` or `read`
  occupies the MCU and may delay a new SCSI selection. No concurrent
  SD-card ownership between two hosts is allowed. This must be
  benchmarked before copying during a critical OS/400 workload.
- Host disconnect or inactivity for 180 seconds closes the export handle
  and unlocks the USB console. During an active transfer no unrelated
  terminal or process should use the CDC interface.
- Completed host destination is never overwritten. Existing `.part`
  files require an explicit `--resume`, and the final hardlink creation
  fails if another process created the target filename in the meantime.
- The transfer does **not** eject, unload, insert, rewrite or delete a
  tape. That state remains under the existing OS/400 and Media Management
  controls.

## Qualification checklist

1. Native C++ packet tests, Python fake-serial downloader tests, and full
   PlatformIO firmware build succeed in CI.
2. Flash only after an OS/400 shutdown; test an offline board or disposable
   media first.
3. Verify `IN_USE` refusal for the currently mounted tape.
4. Finish a small test tape, switch to a new image, and download the old
   one while OS/400 is idle.
5. Check local byte count, SHA-256, and SIMH tape framing. Independently
   compare the downloaded file against the SD image after safe shutdown.
6. Run concurrent disk I/O latency measurements before using this for
   non-idle copying. Do not assume a mere `BUS_FREE` check is sufficient.
