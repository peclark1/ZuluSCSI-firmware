# IBM 9404-B10 firmware integration — upstream 2026.11.05

Based on upstream commit `0b26be69`, preserving local draft PRs #1–#5.

## Tape drive personalities

`Device=AS400_CISC` remains the upstream IBM 4100 tape personality by
default. The early B10's SCSI-1 Tandberg TDC 3600 is **opt-in**:

```ini
[SCSI]
System = AS400_CISC

[SCSI0]
Type = 5
Device = "AS400_CISC"
AS400_TapeModel = TDC3600
TapeDensity = 0x0F
TapeImageFormat = E11
EjectButton = 1
```

Omit `AS400_TapeModel` or set `IBM4100` to preserve the original
IBM 4100 SCSI inquiry, sense, medium type, and vendor pages.

Keep existing disk sections `[SCSI5]` and `[SCSI6]` and their **known-good
image format/alignments** unchanged. In particular, these original packed
520-byte B10 disk images used `AlignUnalignedAccesses = off` per disk.
Changing alignment to `auto` or `cisc` without converting the image alters
the on-SD-card representation and can corrupt reads/writes.

TDC 3600 mode uses its captured 51-byte standard INQUIRY, 24-byte
MODE SENSE(6) page zero, READ BLOCK LIMITS 32768/1, medium type
0x00 loaded / 0x01 empty, and the B10-specific suppression of
medium-change UNIT ATTENTION. Uncaptured proprietary inquiry pages
and unsupported mode pages are rejected; precise sense responses
remain experimental and need validation against the physical drive.

## MODE SELECT regression fixture

B10 to IBM 0671S15, ID 5 and 6:
```text
CDB: 15 11 00 00 16 00
00 00 00 08
00 09 67 C8 00 00 02 08
01 01 04
02 02 01 80
00 01 80
```
Upstream PR #1008 accidentally read successive page lengths from
`idx + 1` instead of `idx_check + 1`. This branch corrects that
and checks the block descriptor bounds, then accepts the profiled
520-byte disk's captured short page 0x02.

## Validation

- [x] Port the five local draft PR patches onto the upstream code
- [x] Add compiled source-fragment regression with real MODE SELECT bytes
- [x] Preserve IBM 4100 as the default; make TDC 3600 explicit
- [ ] Pass full firmware CI and host-side tape tests
- [ ] Validate normal B-mode IPL on backed-up image copies
- [ ] Verify TDC 3600 inquiry, tape E11 reads, media swaps on real B10
- [ ] Resolve directory-mode no-wrap and per-file framing selection
- [ ] Coordinate automatic E11/SIMH detection with upstream maintainers

**Do not flash** onto the working B10 until the known-good firmware,
`zuluscsi.ini`, both packed 520-byte disk images, and checksums have
been preserved. Do not perform destructive LIC installation on the only
copy of the recovered OS/400 disks.
