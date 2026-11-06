# IBM AS/400 9404-B10 — ZuluSCSI Blaster Hardware Validation

**Test date:** November 6, 2026  
**Result:** **PASS — fresh LIC/OS/400 V2R3M0 installation and subsequent manual B-mode IPL**  
**Scope:** Physical 9404-B10, two initially blank 520-byte virtual disks, TDC 3600 emulation, and installation from archived tapes.

## Executive summary

On November 6, 2026, an IBM AS/400 9404-B10 completed a fresh OS/400 V2R3M0 installation using a ZuluSCSI Blaster running the combined AS/400 disk/tape integration firmware. The test began with **two blank, packed 520-byte-sector disk images**, not copies of a bootable system. The B10 initialized and formatted the disks, installed Licensed Internal Code (LIC) using four System Program tape images and the model-unique LIC (MULIC) tape, then completed the OS/400 installation and displayed the **AS/400 Main Menu**. Following a shutdown, the operator explicitly selected **manual B-mode IPL** and again reached the main menu from the newly installed disks.

The operator reported **no firmware-related problems** during the installation or subsequent B-mode IPL. This is an end-to-end validation of this particular firmware/configuration on this physical B10, **not** a claim of error-free behavior for every SCSI command or compatibility with other AS/400 models.

## Tested hardware and firmware

| Item | Observed/test configuration |
|---|---|
| IBM host | IBM AS/400 **9404-B10** (CISC) |
| Device | **ZuluSCSI Blaster**, SCSI target/disk mode, termination enabled |
| Firmware startup identification | `FW Version: 2026.11.05-release 2026-11-06T02:51:39 git:99adb8e` |
| Firmware source | Combined integration branch `integration/as400-b10-upstream-20261105` in `peclark1/ZuluSCSI-firmware` |
| Integration build source commit | `3bf2edcdbba5cdcfcb661f402ff986b5689ded22` (GitHub Actions run metadata); note the firmware's reported `git:99adb8e` build identification is different from the integration branch head SHA |
| CI build | [Successful build run #42152498065](https://github.com/peclark1/ZuluSCSI-firmware/actions/runs/42152498065) |
| Filesystem | FAT32 SD card, approximately 15,256 MB reported by ZuluSCSI |
| SCSI ID 0 | Tape: AS/400 CISC **TDC 3600** identity, `TapeImageFormat=E11`, `TP0/` directory browser |
| SCSI ID 5 | Blank `HD50_520_blank.hda`, IBM 0671S15_200 profile, **520 bytes × 616,392 sectors**, serial `00176201` |
| SCSI ID 6 | Blank `HD60_520_blank.hda`, same profile and geometry, serial `00176202`; **load source** |
| Each blank disk image | **320,523,840 bytes** |
| Disk alignment | `AlignUnalignedAccesses=off` |
| Debug capture | `Debug=1`, `LogToSDCard=1`, serial capture `session.log` |

Firmware initialization log confirmed both images bound to the custom `IBM_0671S15_200` profile, the correct sector geometry, both unique disk serials, the TDC 3600 identity on target 0, and E11 framing for initially loaded tape 1. Startup completed without an error in the supplied initialization excerpt.

### Relevant configuration

```ini
[SCSI]
System = "AS400_CISC"
Debug = 1
DebugIgnoreBusyFree = 0
LogIniSettings = 1
LogToSDCard = 1
IOTrace = 0
EnableUnitAttention = 1
AlignUnalignedAccesses = off

[SCSI5]
AS400_DiskProfile = "IBM_0671S15_200"
AS400_DiskSerialNumber = "00176201"
PrefetchBytes = 4608

[SCSI6]
AS400_DiskProfile = "IBM_0671S15_200"
AS400_DiskSerialNumber = "00176202"
PrefetchBytes = 4608

[SCSI0]
Type = 5
Device = "AS400_CISC"
TapeImageFormat = E11
EjectButton = 1
AS400_TapeModel = TDC3600
```

## Installation media and sequence

The `TP0/` directory contained, in order:

1. `01-AS400_System_Program_V2_R03M00_TAPE1.tap` — 155,821,706 bytes
2. `02-AS400_System_Program_V2_R03M00_TAPE2.tap` — 139,452,938 bytes
3. `03-AS400_System_Program_V2_R03M00_TAPE3.tap` — 13,739,369 bytes
4. `04-AS400_System_Program_V2_R03M00_TAPE4.tap` — 118,444,516 bytes
5. `05-Petes-B10-MULIC-corrected-variable-records.tap` — 35,164 bytes

The operator's live progress monitor showed transitions **tape 1 → tape 2 → tape 3 → tape 4 → MULIC**, matching the observed LIC installation sequence. Tape 2 was successfully read to at least **65%** by the progress monitor, beyond the previously problematic odd-length E11 record near the beginning of that image. Tape 3 reached **95%** before advancing to tape 4; tape 4 was observed at **15%** and subsequently advanced to MULIC. The full installed system later reached the OS/400 main menu.

During the OS/400 installation phase, the operator **reinserted tape 1 as prompted**. The install proceeded through *Restoring programs into library*, *Installing database files in library QSYS*, and an IPL threshold prompt before reaching the main menu. These installation-phase actions are distinct from the earlier LIC tape pass.

**Important limitation:** The directory browser's `TapeImageFormat=E11` setting is target-wide. Earlier separate testing identified the recovered MULIC as SIMH-framed, and numbered-image `TapeImageFormat4=SIMH` can distinguish mixed formats, whereas `TP0/` directory mode does not apply that per-image override. The successful installation demonstrates that the B10 progressed through the MULIC stage **in this configuration**; it does **not** independently prove that every MULIC record was parsed with ideal framing. Preserve the distinction in upstream discussions.

## Storage I/O evidence

The user's AWK summary counted SCSI commands in the captured session by device. The **latest supplied snapshot** was:

| Command | SCSI ID 5 | SCSI ID 6 | SCSI ID 0 |
|---|---:|---:|---:|
| `WRITE(10)` (`0x2A`) | **66,519** | **56,931** | — |
| `READ(10)` (`0x28`) | **30,595** | **48,879** | — |
| `WRITE AND VERIFY` (`0x2E`) | **95** | **2,438** | — |
| `VERIFY` (`0x2F`) | 19 | 11 | — |
| `READ(6)` (`0x08`) | — | — | **6,985** |
| `REQUEST SENSE` (`0x03`) | 2 | 2 | **6,872** |
| `SPACE` (`0x11`) | — | — | **257** |
| `REWIND` (`0x01`) | — | — | **21** |
| `MODE SELECT(6)` (`0x15`) | 0 shown | 2 | 6 |
| `MODE SENSE(6)` (`0x1A`) | 0 shown | 2 | 33 |

Totals for the latest snapshot: **123,450 `WRITE(10)`** commands, **79,474 `READ(10)`** commands across both disks, and **2,533 `WRITE AND VERIFY`** commands. These are **command counts**, not transferred sector/byte counts and not a tally of successful completions. The counts are limited to the logged session (capture began after IPL activity had started) and to the supplied snapshot; they may not cover the entire final IPL.

In a separate uploaded ~28,947-line log excerpt, repeated **SCSI ID 6 `WRITE AND VERIFY` operations** of 48 × 520-byte blocks (24,960-byte payloads) returned `STATUS: 0 GOOD`, with sequential logical block addresses. The same excerpt shows tape `READ(6)` calls, many ending in `CHECK CONDITION` followed by `REQUEST SENSE`; the installer continued. Without decoding the returned sense bytes for each occurrence, do **not** label all of those CHECK CONDITION statuses as errors or as normal end-of-record conditions.

## Confirmed chronology and outcome

| Milestone | Result / observation |
|---|---|
| Firmware initializes target 0/5/6 | **PASS** — verified in startup log |
| Both blank 520-byte disks recognized | **PASS** — IDs 5 and 6, geometry and profiles correct |
| LIC installation reads four V2R3M0 volumes | **PASS** — live tape progress and media transitions observed |
| MULIC stage reached | **PASS** — expected next tape requested/selected; installation continued |
| Disk writes and verification | **PASS** for substantial observed successful operations; both disks handled sustained I/O |
| System password bypass | **PASS** — operator chose bypass (did not apply the password from existing archival disk images) |
| Disk initialization/formatting | **PASS** — operator observed disk 2 formatting completion |
| OS/400 program/library restoration | **PASS** — QSYS restoration and later main menu reached |
| Fresh OS/400 V2R3M0 installation | **PASS** — AS/400 Main Menu displayed |
| Independent normal IPL | **PASS** — operator explicitly confirmed **manual B-mode IPL** followed by AS/400 Main Menu |
| Firmware issues during tested workflow | **None reported by operator**; full log error/sense analysis not yet complete |

The later system-password bypass screen showed stored system serial `10-14128`, machine type/model `9404 / B10`, processor-card serial `10-8918070`, and password version `QZ103E0A`; the operator observed that the password version differed from the previous installed image (`QZ103D08`). This password observation is incidental to SCSI validation; no claim is made about its cause.

## What this does and does not validate

**Strongly supported:** the combined build supports a fresh LIC/OS/400 install and a subsequent manual B-mode IPL on this IBM 9404-B10 using the specified two 520-byte disk targets and TDC 3600/E11 installation media workflow. The recorded command counts demonstrate extended real-host SCSI traffic on both disks and the tape target.

**Not yet established:** every individual SCSI command's status/sense correctness; an exhaustive disk/tape error audit of the full capture; standalone isolated-PR builds on B10; mixed-format MULIC parsing correctness for directory mode; write-capable scratch-tape `SAV`/`RST` round trip; explicit `IMGn` end-of-sequence behavior; long-term soak/normal application workload; physical Model 600 non-regression. The success of this **combined integration build** must not be attributed to the two upstream PR branches as if each had independently been tested on hardware.

## Proposed next steps

1. **Archive test state** after a clean shutdown: both installed `.hda` images, `zuluscsi.ini`, firmware artifact, startup log, serial `session.log`, monitor output, and main-menu photographs. Compute SHA-256 for both newly installed images and the firmware ZIP/UF2. Keep prior known-good images separate.
2. **Analyze the serial log** to produce per-target command/status totals, unusual sense keys/ASC/ASCQ, tape volume-change timeline, disk MODE SELECT outcomes, and any `CRASH!`, timeout, or I/O error lines. Record that capture was enabled after the IPL began.
3. **Optional separate tape tests:** on expendable scratch media, perform OS/400 tape save/restore, write filemarks, rewind, space, read, and media-change tests. Do not write to the archival installation tapes.
4. **Independent regression:** request Alex's non-destructive Model 600 test with its own baseline settings and preserved disk images; no result is claimed yet.
5. **Upstream reporting:** link this report as combined-build B10 hardware evidence to [MODE SELECT PR #1028](https://github.com/ZuluSCSI/ZuluSCSI-firmware/pull/1028), [TDC 3600/E11 PR #1029](https://github.com/ZuluSCSI/ZuluSCSI-firmware/pull/1029), and [integration draft PR #6](https://github.com/peclark1/ZuluSCSI-firmware/pull/6). Keep PR-specific independent tests distinct.

## Source records

- Operator's November 6, 2026 real-hardware session observations and manual confirmation of successful **B-mode IPL**.
- ZuluSCSI startup log supplied in this project, firmware identification `2026.11.05-release ... git:99adb8e`.
- Operator-supplied command counters and a serial-log excerpt containing disk `WRITE AND VERIFY` `GOOD` completions.
- [Integration acceptance checklist](https://github.com/peclark1/ZuluSCSI-firmware/blob/integration/as400-b10-upstream-20261105/docs/AS400_B10_MODEL600_HARDWARE_ACCEPTANCE.md).
- [Integration GitHub Actions build](https://github.com/peclark1/ZuluSCSI-firmware/actions/runs/42152498065), recorded successful for branch commit `3bf2edcdbba5cdcfcb661f402ff986b5689ded22`.

**Report status:** Hardware acceptance test completed; full artifact preservation and detailed log audit pending.
