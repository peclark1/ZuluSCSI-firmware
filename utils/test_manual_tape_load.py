#!/usr/bin/env python3
"""Compile and test the actual manual-tape eject/insert functions from firmware.

This is a host-side behavior harness, not an emulation of the AS/400 SCSI bus.
The production functions are extracted verbatim and compiled with minimal fake
device/configuration objects. A full PlatformIO build and real hardware test
are still required.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DISK_SOURCE = ROOT / "src/ZuluSCSI_disk.cpp"


def extract_function(source, name):
    start = source.index("void " + name + "(")
    brace = source.index("{", start)
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError("Function body is incomplete: " + name)


HARNESS_HEAD = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>

enum {
    S2S_CFG_FIXED = 0,
    S2S_CFG_REMOVABLE = 1,
    S2S_CFG_SEQUENTIAL = 5,
    S2S_CFG_TARGET_ID_BITS = 7,
    S2S_CFG_ENABLE_UNIT_ATTENTION = 1,
    S2S_CFG_QUIRKS_AS400 = 7,
    POWER_ON_RESET_OR_BUS_DEVICE_RESET_OCCURRED = 100,
    NOT_READY_TO_READY_TRANSITION_MEDIUM_MAY_HAVE_CHANGED = 101,
    IMAGE_INDEX_MAX = 9
};

struct MockFile {
    bool open = false;
    int closes = 0;
    bool isOpen() const { return open; }
    void close() { open = false; closes++; }
};
struct image_config_t {
    MockFile file;
    int deviceType = S2S_CFG_SEQUENTIAL;
    int scsiId = 0;
    int quirks = 0;
    int image_index = 0;
    bool ejected = false;
    bool reinsert_after_eject = false;
    bool ejectFixedDiskReadOnly = false;
    bool ejectFixedDiskWriteBlocked = false;
};
struct DeviceConfig {
    bool manualTapeLoad = false;
    bool tapeImageLoop = true;
    bool reinsertImmediately = false;
    int ejectBlinkTimes = 20;
    int ejectBlinkPeriod = 50;
};
struct {
    DeviceConfig devices[8];
    DeviceConfig* getDevice(int id) { return &devices[id]; }
} g_scsi_settings;
struct {
    struct { int flags = 0; } boardCfg;
    struct { int unitAttention = 0; } targets[8];
} scsiDev;

int next_switches = 0;
int rewinds = 0;
template<typename... T> void dbgmsg(T...) {}
template<typename... T> void logmsg(T...) {}
void blink_cancel() {}
void blinkStatus(int, int) {}
void tapeRewind(image_config_t&, int) { rewinds++; }
bool isAS400TDC3600TapeIdentity(int) { return true; }
bool switchNextImage(image_config_t &img) {
    next_switches++;
    img.file.open = true;
    return true;
}
"""

HARNESS_TAIL = r"""
int main() {
    image_config_t tape;
    tape.file.open = true;
    tape.reinsert_after_eject = true;
    // Default: automatic loading stays enabled and stages a successor.
    assert(!g_scsi_settings.getDevice(0)->manualTapeLoad);
    doPerformEject(tape);
    assert(next_switches == 1);
    assert(tape.ejected);
    assert(tape.file.isOpen());
    scsiDiskCloseTray(tape);
    assert(!tape.ejected);

    // Manual: host UNLOAD leaves the drive empty, without advancing.
    g_scsi_settings.getDevice(0)->manualTapeLoad = true;
    next_switches = 0;
    tape.file.open = true;
    tape.ejected = false;
    tape.reinsert_after_eject = true;
    doPerformEject(tape);
    assert(tape.ejected);
    assert(!tape.file.isOpen());
    assert(!tape.reinsert_after_eject);
    assert(next_switches == 0);

    // LOAD, INQUIRY and serial Insert all share this insertion function.
    scsiDiskCloseTray(tape);
    assert(tape.ejected);
    tape.reinsert_after_eject = true; // even a stale reinsert request is safe
    scsiDiskCloseTray(tape);
    assert(tape.ejected);
    assert(next_switches == 0);

    // Repeated host UNLOAD must not reinsert the old cartridge.
    doPerformEject(tape);
    assert(tape.ejected && !tape.file.isOpen());
    assert(next_switches == 0);

    // Explicit operator choice reopens the image: insertion is now allowed.
    tape.file.open = true; // controlLoadImage's successful image-open effect
    scsiDiskCloseTray(tape);
    assert(!tape.ejected);
    assert(rewinds >= 2);

    // Other removable devices must retain automatic behavior.
    image_config_t removable;
    removable.deviceType = S2S_CFG_REMOVABLE;
    removable.file.open = true;
    doPerformEject(removable);
    assert(next_switches == 1 && removable.ejected);

    // Manual settings on one target do not affect another tape target.
    image_config_t otherTape;
    otherTape.scsiId = 1;
    otherTape.file.open = true;
    doPerformEject(otherTape);
    assert(next_switches == 2);

    std::puts("PASS: 8 manual/default tape media policy checks");
    return 0;
}
"""


def main():
    source = DISK_SOURCE.read_text()
    code = (
        HARNESS_HEAD
        + "\n"
        + extract_function(source, "scsiDiskCloseTray")
        + "\n"
        + extract_function(source, "doPerformEject")
        + "\n"
        + HARNESS_TAIL
    )
    with tempfile.TemporaryDirectory(prefix="manual-tape-") as temp:
        sourcefile = Path(temp) / "test_manual_tape.cpp"
        exe = Path(temp) / "test_manual_tape"
        sourcefile.write_text(code)
        subprocess.run(
            [os.environ.get("CXX", "c++"), "-std=c++17", "-DPLATFORM_AS400",
             "-Wall", "-Wextra", "-Werror", str(sourcefile), "-o", str(exe)],
            check=True,
        )
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
