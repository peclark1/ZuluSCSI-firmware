# AS/400 ZuluSCSI tape-handling decisions and upstream cleanup

Status: working notes, November 5, 2026. Target system: real IBM 9404-B10, OS/400 V2R3M0.

## Verified configuration decisions

- Prefer a `TP0/` directory for the SCSI ID 0 tape drive. The firmware automatically discovers this directory. **No `ImgDir=TP0` setting is needed.**
- Do not use explicit `IMG0`–`IMGn` declarations in the preferred workflow: they don't populate the interactive media browser. Use the directory browser instead.
- Omit `TapeImageLoop` from the current `TP0/` setup. Current implementation only checks it for the explicit IMG-list path, so it **does not disable directory wraparound**.
- Keep `TapeImageFormat=E11` for V2R3M0 installation tapes.
- Keep only `.tap` files in `TP0/`. The media browser unexpectedly offered JPEG files; ZIP was ignored. Name files `01-...`, `02-...`, etc. for automatic ordering, but **always verify the filenames** when selecting: interactive list numbers appeared in filesystem enumeration order.
- Manual image selection via `m` → device 0 → `l` does not necessarily insert the medium. If `[ejected]` is shown, explicitly issue `i` and confirm that `[ejected]` disappears before replying `1=Continue` to OS/400's CPA2055 prompt.

Suggested minimal configuration:
```ini
[SCSI0]
Type = 5
Device = "AS400_CISC"
TapeImageFormat = E11
EjectButton = 1
```

## Firmware engineering / review checklist

- [ ] Sync fork with Alex's latest upstream ZuluSCSI firmware; preserve the known-working tested branch and carefully review upstream overlap.
- [ ] Decide whether finite-sequence / no-wrap support is needed for `TP0/`. Today `findNextImageAfter()` wraps directory images regardless of `TapeImageLoop`; the opt-in setting only affects `IMGn`.
- [ ] If no-wrap is retained, ensure explicit operator selection can always load a tape after the end-of-sequence condition.
- [ ] **Ask Alex for his recommendation on automatic E11-vs-SIMH framing detection.** Would he prefer auto-detection per mounted tape, a per-filename override, or an explicit setting? Ask what evidence is sufficient to distinguish the formats safely, especially when all tape records have even lengths (E11 and padded SIMH can then be byte-identical), and how ambiguous/invalid data should be handled without silently corrupting a restore. Consider offering an explicit override as a fallback even if an automatic detector is added. Document real B10 examples and tests covering variable-length and odd-length records, filemarks, and truncated/error cases.
- [ ] Fix mixed E11/SIMH selection in directory mode. The four installation volumes are E11; the recovered B10 MULIC was configured with `TapeImageFormat4=SIMH` in the former `IMGn` workflow. Index-based overrides are unsafe for arbitrary manual selections. Prefer an explicit per-file format mapping or tested automatic detection.
- [ ] Review tape `START STOP UNIT` handling for `LoEj=0 Start=0`; currently this ejects and automatically advances to the next image. Do not change the proven IPL behavior without hardware tests.
- [ ] Consider filtering non-tape extensions from the tape media browser.
- [ ] Add concise documentation for the operator's select → insert → verify → reply workflow, including CPA2055.
- [ ] Reconcile AS/400-specific disk, LIC, SCSI tape, and sense-data work with upstream; prepare focused upstream PRs.
- [ ] Build/test current upstream-based Blaster firmware and record real-hardware results before submission.

## Evidence and preservation

- The real 9404-B10 successfully installed and ran **OS/400 V2R3M0**, confirmed by `DSPSFWRSC` for LIC and OS.
- `WRKDSKSTS` showed two 315 MB type-6100 disks, with allocated storage.
- Successful tape workflow: auto advance of directory image, manually insert image if ejected, then respond to `CPA2055` next-volume prompt.
- [ ] Confirm clean completion of controlled `PWRDWNSYS OPTION(*CNTRLD) DELAY(600) RESTART(*NO)`.
- [ ] Preserve matched backups and SHA-256 checksums of `HD50_520_D2_IMAGE_ORIGINAL.hda`, `HD60_520_D1_IMAGE_ORIGINAL.HDA`, plus `zuluscsi.ini`, the exact firmware build ID, and logs.
- [ ] After backup, confirm a normal B-mode disk IPL before updating firmware.

Related draft PRs:
- #4 https://github.com/peclark1/ZuluSCSI-firmware/pull/4 — finite tape image sequence
- #5 https://github.com/peclark1/ZuluSCSI-firmware/pull/5 — per-IMG tape framing override

This is a development checklist, not yet an upstream-ready specification. Preserve the successful runtime build until a replacement is validated.
