# Model 600 independent firmware regression test — quick card

**Purpose:** Verify that the upstream-2026.11.05 integration of the
9404-B10 fixes does **not** disrupt a newer AS/400 Model 600.
This test is intended for Alex's own Model 600 setup, not for the B10.

## Before testing

- [ ] Save Alex's **known-good** UF2/bin firmware, `zuluscsi.ini`,
  and all original card contents to a different storage device.
  Record SHA-256 and current boot/device status.
- [ ] Keep the original SD/card untouched. Use a **second test SD/card**
  with verified copies of working images and his existing `zuluscsi.ini`.
- [ ] Wait for the CI run on [integration PR #6](
  https://github.com/peclark1/ZuluSCSI-firmware/pull/6)
  to **finish successfully**. Download the appropriate hardware target
  only from that passing PR's **ZuluSCSI UF2s** / firmware artifacts.
  The focused MODE SELECT-only upstream PR #1028 is a **different** build.
- [ ] Log the integrated firmware commit SHA on boot and note the
  hardware model and connected SCSI device IDs.

**Important:** Do not copy the B10 `zuluscsi.ini`. Do not enable
`AS400_TapeModel=TDC3600` on the Model 600. Preserve Alex's existing
per-device disk profiles and alignment, including any 522-byte-sector
devices; changing alignment modes without converting the images would
change their storage representation. Do not initialize or reformat
existing disks.

## Non-destructive smoke test

1. Boot the Model 600 from copied known-good images using the PR #6
   firmware. Record the firmware identifier, normal/abnormal SRCs,
   and whether the host identifies every expected SCSI device.
2. Verify a normal IPL and disk access. Compare recognized device
   types, serials, disk capacity/sector size, mode-page behavior,
   and ordinary read operations to the known-good baseline.
3. For a **disposable** test object/library or scratch disk, perform
   a small write and readback. Do not touch user data during testing.
4. If Alex has a supported tape target, test with an expendable tape
   image: INQUIRY, MODE SENSE, load/unload, REWIND, filemarks,
   and small save/readback. Avoid touching archival tapes.
5. Check debug logs for unexpected CHECK CONDITION, UNIT ATTENTION,
   MODE SELECT failure, block-size changes, or disk-image remapping.
6. Perform a controlled shutdown and repeat a normal IPL to confirm
   no persistence-related regression.

**Stop conditions:** unexpected SRC; missing disk; different disk
identity or sector size; data mismatch; unexpected destructive operation.
Power down normally where possible, preserve logs and the test card, and
restore the known-good firmware and original card. Do **not** run a LIC
installation or disk initialization as part of this smoke test.

## Reply template

```text
AS/400 model / IOP:
ZuluSCSI hardware target:
Baseline firmware -> tested integration commit:
OS/400 version:
SCSI IDs / device identities / sector sizes:
Baseline SD/image backups and hashes checked: yes/no
Normal IPL: PASS/FAIL (SRC)
Disk read and scratch write/readback: PASS/FAIL
Tape (if configured): PASS/FAIL/NOT TESTED
Controlled shutdown + second IPL: PASS/FAIL
Relevant log entries, SCSI commands, sense, SRCs:
```

For the B10 full fresh-install protocol, see
[AS400_B10_MODEL600_HARDWARE_ACCEPTANCE.md](
AS400_B10_MODEL600_HARDWARE_ACCEPTANCE.md).
