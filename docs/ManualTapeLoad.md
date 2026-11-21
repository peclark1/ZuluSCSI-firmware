# ManualTapeLoad: operator-controlled removable tape changes

On tape targets (`Type = 5`), set `ManualTapeLoad = 1` to require the
operator to choose the next cartridge after the host unloads the current tape.
The default is `0`, preserving the existing automatic image-advance behavior
for all existing users and installation sets.

## Configuration

Example for a tape device on SCSI ID 0:

```ini
[SCSI0]
Type = 5
ImgDir = TP0
TapeImageFormat = SIMH
ManualTapeLoad = 1
```

Keep any existing AS/400 tape profile, product identity, and other device
settings unchanged. Select SIMH or E11 according to the actual image framing.

- At startup ZuluSCSI still mounts the first image from `TP0/` normally.
  This can be selected explicitly from the media menu before starting the host.
- On SCSI UNLOAD, the current image is closed, the tape is ejected, and the
  virtual drive reports **NOT READY / MEDIUM NOT PRESENT** (`02/3A00`).
- Automatic selection of the next filename is skipped. Host LOAD, INQUIRY,
  TEST UNIT READY, repeated UNLOAD, and serial `Insert` do not revive the
  previous cartridge while no image is open.
- The USB serial **Media Management** menu remains usable: choose the tape
  target, list images (`l`), then select an image number. Explicit selection
  reopens that image and stages it for insertion on the host's next poll.
  The `n` command is also an explicit operator choice to advance.
- You can select images in any order and select a previously used image again.
  The directory listing does not depend on `IMG0`/`IMG1` numbering.
- `TapeImageLoop` continues to control normal explicit `IMG`-sequence
  wrapping when automatic loading is enabled; it does not enable automatic
  advancement while `ManualTapeLoad = 1`.
- Other device types and tape targets without this setting keep their
  existing behavior. The B10-specific UNIT ATTENTION compatibility logic
  is untouched.

## AS/400 9404-B10 acceptance test

Use **disposable virtual disk images** for installation or D-mode IPL work,
and preserve the original tapes / raw images unchanged.

1. With the AS/400 powered off, save the current `zuluscsi.ini` and update
   the tape target's section to `ManualTapeLoad = 1`.
2. Boot ZuluSCSI, confirm the expected tape at startup using Media Management,
   or explicitly select the first SAVSYS tape before the IPL.
3. Start D-mode IPL. When the AS/400 unloads the tape, verify the serial log
   reports waiting for operator selection, not switching to the next file.
4. Confirm repeated host readiness polls return `02/3A00` and the drive
   remains empty; note the AS/400 front-panel media-request SRC.
5. In the USB serial media menu, list images and select the requested volume
   (which might be SAVSYS vol. 2, model-unique LIC / MULIC, or vol. 1 again).
6. Verify that the selected image is inserted on the next host poll and that
   OS/400 continues. Repeat as needed.
7. Separately confirm `ManualTapeLoad = 0` still automatically advances
   through a prepared multi-tape installation set.

**Hardware validation is still required.** The source-level tests and CI
firmware build do not establish the B10's IPL or timing behavior. Do not
restart ZuluSCSI while the AS/400 is accessing disks hosted by it.
