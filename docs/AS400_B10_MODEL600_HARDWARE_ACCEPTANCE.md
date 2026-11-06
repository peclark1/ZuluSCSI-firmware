# AS/400 hardware acceptance: IBM 9404-B10 and Model 600

Status: **planned hardware validation**. The upstream-v2026.11.05
integration branch has passed firmware compilation and host regressions,
but is not yet validated on both physical systems.

Integration: [draft PR #6](https://github.com/peclark1/ZuluSCSI-firmware/pull/6)
Focused MODE SELECT: [draft fork PR #7](https://github.com/peclark1/ZuluSCSI-firmware/pull/7)

## Non-negotiable safeguards

1. Keep the working B10 firmware, complete SD card contents, `zuluscsi.ini`,
   and **both 520-byte disk images** intact and backed up off-card.
   Record SHA-256 of each image and known-good UF2.
2. Use a **different SD card** for the experimental firmware installation.
   Never perform LIC installation/initialization on the original images.
   Confirm the test SD has **no working-system disk images** on it.
3. Keep sector alignment and disk identity unchanged. Packed B10 520-byte
   images require `AlignUnalignedAccesses=off` on each disk target; switching
   to `cisc` or `auto` without converting the images is incompatible.
4. Record the integration commit SHA reported at firmware startup. Download
   the Blaster UF2 from **PR #6's passing GitHub Actions build artifacts**,
   not from the upstream release or the MODE SELECT-only PR #7 build.
5. A firmware build/host test passes only software checks. Do not describe
   a device as hardware-verified until its actual SCSI exchanges pass.

## B10 — first a non-destructive smoke test

- Create the test SD card with *copies* of both known-good B10 disk images
  and a copy of the matching INI settings. Confirm two SCSI disk IDs (6
  load source, 5 second disk) and tape ID 0; do not install or initialize.
- Select `AS400_TapeModel=TDC3600` only on the tape target, retain
  `Device=AS400_CISC` and the existing E11 tape settings. Keep every
  disk profile/serial and alignment setting identical to the successful
  earlier firmware.
- Observe standard INQUIRY (captured 51-byte TDC 3600 personality),
  MODE SENSE page 0, READ BLOCK LIMITS 32768/1, and loaded/ejected
  medium types 0x00/0x01. Some unsupported-page sense codes remain
  experimental; capture the CDB and actual sense rather than assume
  they match the physical drive.
- Boot in **normal B mode**. Verify `WRKDSKSTS`, `DSPSFWRSC`,
  and any existing scratch-library read. Shut down cleanly, retaining
  the working image copies. If this fails, stop before fresh install.

## B10 — full installation with blank disk images (destructive *test* media)

1. On the isolated test SD, copy the working INI's disk sections, preserving
   both the 0671S15 personality, **different valid serial numbers**, and
   `AlignUnalignedAccesses=off`. Move *all existing disk images* off this
   test SD before generating the blank pair. Avoid image-name collisions.
2. For the known-good 0671S15 layout, create exactly two zero-filled
   616,392 × 520-byte test disks (**320,523,840 bytes each**). The example
   below must run in a *new, empty, test-only staging directory* and refuses
   to replace files if they already exist:

   ```bash
   python3 - <<'PY'
   from pathlib import Path
   for name in ("HD60_520_INSTALL_TEST.hda", "HD50_520_INSTALL_TEST.hda"):
       with Path(name).open("xb") as f:
           f.truncate(616_392 * 520)
       print(name, Path(name).stat().st_size)
   PY
   ```

   Copy these onto the test SD root and verify their sizes. Do not use
   this on the baseline card.
3. Put the **four unmodified V2R3M0 E11 .tap files** in `TP0/`,
   preserving their original names and order. The effective tape
   settings (merge into existing `[SCSI0]`, not a replacement INI) are:

   ```ini
   [SCSI0]
   Type = 5
   Device = "AS400_CISC"
   AS400_TapeModel = TDC3600
   TapeImageFormat = E11
   ```

   Keep proven tape-density and other per-target settings. Confirm a
   tape is inserted in the browser, not merely selected: if the display
   says `[ejected]`, issue `i` before acknowledging OS/400's
   next-volume inquiry.
4. Start the installation/LIC initialize sequence in D mode against the
   *test-only disks*. Track first disk MODE SELECT, initialization,
   first and subsequent tape reads, SPACE, tape marks and every
   `CPA2055` volume-change prompt. Record any front-panel SRC,
   CHECK CONDITION, sense bytes, tape image/offset and log timestamp.
5. Complete OS/400 V2R3M0 installation, verify both disks with
   `WRKDSKSTS` and the level with `DSPSFWRSC`, shut down using
   `PWRDWNSYS OPTION(*CNTRLD) DELAY(600) RESTART(*NO)`,
   then IPL from the new installation in normal B mode.
6. Create a disposable library with a known small object, save it to a
   **separate writable scratch virtual tape**, delete/rename only the
   disposable copy, restore it, and compare object contents. This
   independently exercises WRITE, WRITE FILEMARKS, REWIND, SPACE, READ,
   and media-change behavior; never write to the archived install tapes.

### Separate tape-image selection checks

- **Directory/browser `TP0/`**: exercise explicit select, eject, and
  insert; record each actual selected filename and medium state. Directory
  wraparound is **not** controlled by `TapeImageLoop=0`.
- **Explicit `IMG0`..`IMG3`** (on a separate test configuration):
  `TapeImageLoop=0` should stop after the last image rather than wrap
  back to the first. This behavior is **not** implied by the directory
  mode and must be checked separately.
- Test mixed E11 and conventional SIMH tapes with `TapeImageFormatN`
  only when the specific tape files are known and backed up. Keep
  installation tapes all E11; filename `.tap` does not identify
  padding semantics.

## Model 600 — Alex's independent regression test

**Do not copy the B10 INI into the Model 600.** The Model 600's SCSI
adapter, bus IDs, disk block size, profile, and OS/400 release must come
from Alex's own known-good setup. The `AS400_TapeModel=TDC3600` setting
is for the older CISC B10 only; omit it on the Model 600. In particular,
do not switch 520/522-byte layouts or `AlignUnalignedAccesses` on any
existing Model 600 image.

1. Preserve the Model 600's known-good SD contents, firmware, INI and
   SHA-256 image hashes; test with copies, not only source images.
2. Identify its device IDs, disk profile(s), alignment, SCSI bus/adapter
   and original firmware version; check that none unexpectedly changed
   after flashing the **PR #6 integration build**.
3. Perform a non-destructive normal IPL. Confirm expected disk and tape
   identification, device readiness and lack of unexpected SRCs.
4. Compare read/write behavior on a **scratch device or disposable test
   object** to the baseline. Exercise tape if applicable, with a scratch
   tape image rather than archival media.
5. Record any behavior difference in INQUIRY, MODE SENSE/SELECT,
   READ/WRITE, UNIT ATTENTION and media swap. The Model 600 test is
   particularly valuable for checking that B10-specific fixes do not
   change newer AS/400 behavior.

An OS/400 reinstall on the Model 600 is **not** required for an initial
regression check unless Alex elects to perform one with disposable media.

## Results to report back

| Field | IBM 9404-B10 | Model 600 |
|---|---|---|
| Firmware commit and build | pending | pending |
| Hardware board and SCSI connection | ZuluSCSI Blaster; confirm | Alex to specify |
| Baseline image SHA-256 verified | pending | pending |
| Device IDs / block size / layout | 6+5; 520; packed | Alex to specify |
| Normal IPL on copied existing media | pending | pending |
| New blank-disk LIC/OS/400 install | pending | optional |
| Tape identity and medium-change behavior | pending | if applicable |
| Tape file switching and E11/SIMH | pending | if applicable |
| Scratch save/restore | pending | if applicable |
| SRCs and relevant SCSI log excerpts | pending | pending |

**Acceptance:** successful B10 fresh install, controlled shutdown and
normal IPL, meaningful tape read/write/restore, plus no regression on
Model 600. Preserve the old firmware and disk images until both systems
have passed or failures are understood.
