# Experimental IBM 0671S15 saved MODE pages (B10)

**Development-only; not a demonstrated fix for B601 0430.** This feature is
separate from read-only ZPDB profiles and does not make VPD writable.

## Scope and opt-in

The default is **off**. On a spare SD card, for a profiled fixed disk using an
IBM 0671S15-compatible captured 22-byte MODE SENSE structure, 520-byte block
geometry, and a unique stable `AS400_DiskSerialNumber` override:

```ini
[SCSI]
AS400_PersistModePages = 1

[SCSI5]
AS400_DiskProfile = "IBM_0671S15_200"
AS400_DiskSerialNumber = "00176201"

[SCSI6]
AS400_DiskProfile = "IBM_0671S15_200"
AS400_DiskSerialNumber = "00176202"
```

Actual image names, device settings, and disk contents remain unchanged.
Keep existing, hardware-verified INI settings. This is a **single global
opt-in**, restricted internally to a recognized 22-byte IBM short-page capture.
It does not enable saved parameters for tape or other disk models.

The currently modeled short pages are captured PS=1 pages `01 01 XX` and
`02 02 XX XX`; captured page 00 is returned but not modifiable. Each disk
holds independent current and saved copies. MODE SELECT(6)/(10) PF=1/SP=0
changes current only, SP=1 also saves, and MODE SENSE(6)/(10) page control:
- PC=0: current
- PC=1: experimental emulator changeability mask (FF for modeled page data,
  zero for page 00; **not asserted to match a physical 0671S15**)
- PC=2: immutable captured defaults from ZPDB
- PC=3: saved state (captured defaults until the host saves a modification)

All-pages (3F) and individual pages 00, 01, 02 are handled. Unknown pages,
duplicate pages, a changed page-00 parameter, malformed MODE SELECT lengths,
invalid PF, and altered block descriptors return CHECK CONDITION. The
captured B10 request `15 11 00 00 16 00` and its full 22-byte list are
accepted unchanged.

**Guardrails:** The 8-byte block descriptor must exactly match the captured
profile, including 520-byte sector length. This overlay does not call
`s2s_configSave()`, modify the image, INQUIRY, or VPD, and does not rewrite
ZPDB or its flash settings area. No page-specific *operational* effects are
claimed beyond readback and saving; real 0671S15 changeability and side effects
need additional traces.

## Storage, recovery and lifecycle

Two alternating 32-byte CRC-protected records live under `/as400_mode/`
on the SD filesystem. Their names are a stable 64-bit FNV-1a identity hash of
the *profile name, configured eight-byte IBM serial, and immutable 22-byte
MODE SENSE baseline*, plus the suffix
`a` or `b`. The active record has the newest valid sequence number.
Writes go to the **inactive** file, are synced/closed and independently
read back before returning GOOD. Torn/corrupted records, wrong version and
wrong identity are ignored; the previous valid record remains usable. Saved records are read and validated during the device/profile initialization
scan, before SCSI commands begin; subsequent MODE SENSE responses use RAM.
Without a valid record, the captured defaults apply. If no SD write can be verified,
MODE SELECT with a changed SP=1 value returns CHECK CONDITION/HARDWARE ERROR,
rather than acknowledging a save that failed.

No 520-byte sector/image file is opened for state writing. The settings
region at RP2MCU offset 1183744 (16 KiB) is **not used**: it is reserved but
does not yet have an established transactional or lifetime policy for mutable
host writes. The SD-card journal is a deliberate experiment with lower risk
to firmware and ROM drive layout.

The state is SD-card-local: copying or swapping an image to a second SD card
does **not** automatically copy saved parameters. Move paired state files
intentionally if reproducing a saved configuration. Removing both files
restores captured defaults. Changing the serial/profile or the captured MODE SENSE baseline intentionally changes
the persistent identity. Do not reuse the same profile + serial for two
simultaneously presented disks. The feature checks this and rejects duplicate
identity use when detected. Avoid changing identity while ZuluSCSI is running.

## Regression tests and limitations

```sh
python3 utils/test_as400_mode_select_length.py
python3 utils/test_as400_mode_pages.py
python3 utils/test_e11_tapes.py
```

The second command host-compiles the **same** mode-page and CRC/record code
used by the firmware, then tests captured MODE SELECT(6), 10-byte conversion,
PC 0/1/2/3, individual/all pages, SP=0 vs SP=1, multi-target isolation,
reinitialize-from-saved, malformed input, 520-byte descriptor protection,
corrupted records, version mismatch and torn inactive-slot recovery. Firmware
CI builds all configured targets. Power-loss recovery of the physical SdFat
write path, timing under real B10 command load, and the *physical drive's*
actual changeable mask are not covered by host tests.

## Safe physical B10 validation (only after owner approves flashing)

1. Keep the known-good UF2/INI and the working 520-byte SD card untouched.
   Shut down the B10 normally, then power it off before touching ZuluSCSI/SD.
2. Back up the complete failed-V3R1 SD *before any further IPL*; follow
   `B10_V3R1_IPL_Diagnostic_Plan_2026-11-11.md` for pre/post-Solstice
   comparison and dump-preservation priorities.
3. Prepare a **different** SD card containing verified copies of a
   known-good, **non-unique-use** disk image and original drive identities.
   Record image sizes and SHA256 before and after. Confirm both IDs 5/6 use
   their own IBM serial and that `AS400_PersistModePages=1` is set only in
   the experimental INI.
4. Before connecting the B10, issue synthetic MODE SENSE and MODE SELECT
   commands from a test host against a **sacrificial ZuluSCSI image**, comparing
   PC=0/1/2/3 and SP=0/1, then power cycle ZuluSCSI and repeat. Check that
   `/as400_mode/` has two slots as saves alternate and that other IDs do not
   inherit changes. Never send MODE SELECT to an original physical IBM disk
   in these tests.
5. Only then perform a **controlled** B10 IPL on expendable SD image copies,
   capture full SCSI commands/status/sense and SRCs, and compare with the
   known-good unmodified firmware run. Do not repeatedly trigger dumps or
   presume `B601 0430` was caused by saved MODE parameters.
6. If differences are unexpected, power down and revert to the known-good
   firmware and original SD; do not erase, truncate, repair, initialize, or
   convert the original configured ASPs.

This is a *proof-of-concept MODE-page emulation feature*, not an assertion
that the real 0671S15 accepts arbitrary bit changes on pages 01/02.
