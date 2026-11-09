#!/usr/bin/env python3
"""Safely fetch a closed SIMH .tap image over ZuluSCSI's USB serial interface.

Firmware ZT1 protocol: 'f' enters an exclusive read-only session. The host
sends OPEN <id> <basename>, READ <offset> <length>, CLOSE and QUIT.
READ responses are binary ZT1D packets with offset/length/CRC32. Every
chunk is verified, and completed output is atomically renamed.
Requires: pip install pyserial (for actual serial port use).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time
import zlib

MAGIC = b"ZT1D"
HEADER = struct.Struct("<4sQHI")
MAX_CHUNK = 1024


class ExportError(Exception):
    pass


class TapeClient:
    def __init__(self, serial_port, timeout=120):
        self.port = serial_port
        self.timeout = timeout

    def _send(self, data):
        self.port.write(data)
        self.port.flush()

    def _line(self):
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            raw = self.port.readline()
            if not raw:
                continue
            # Reject malformed replies rather than accepting serial logs
            # as results from this dedicated binary protocol.
            if raw.startswith(b"ZT1 "):
                if raw.startswith(b"ZT1 ERR "):
                    raise ExportError(raw.decode("ascii", "replace").strip())
                return raw.decode("ascii", "strict").strip()
        raise ExportError("Timed out waiting for ZuluSCSI response")

    def connect(self):
        self.port.reset_input_buffer()
        self._send(b"f")
        deadline = time.monotonic() + min(self.timeout, 30)
        while time.monotonic() < deadline:
            raw = self.port.readline()
            if b"ZT1 READY" in raw:
                return
        raise ExportError("No ZT1 READY; exit any active serial menu first, and verify firmware")

    def open(self, scsi_id, basename):
        if not (0 <= scsi_id <= 15) or not safe_name(basename):
            raise ExportError("SCSI ID or tape basename invalid; use filename.tap, no paths")
        self._send(f"OPEN {scsi_id} {basename}\n".encode("ascii"))
        reply = self._line()
        if not reply.startswith("ZT1 SIZE "):
            raise ExportError("Unexpected OPEN response: " + reply)
        try:
            size = int(reply.removeprefix("ZT1 SIZE "))
        except ValueError as exc:
            raise ExportError("Bad tape size response: " + reply) from exc
        if size < 0:
            raise ExportError("Negative tape size")
        return size

    def _exact(self, count):
        parts = []
        left = count
        deadline = time.monotonic() + self.timeout
        while left and time.monotonic() < deadline:
            buf = self.port.read(left)
            if buf:
                parts.append(buf)
                left -= len(buf)
        if left:
            raise ExportError(f"Short USB transfer: {left} bytes missing")
        return b"".join(parts)

    def read(self, offset, size):
        if size < 1 or size > MAX_CHUNK or offset < 0:
            raise ExportError("Invalid block range")
        self._send(f"READ {offset} {size}\n".encode("ascii"))
        prefix = self._exact(4)
        if prefix == b"ZT1 ":
            raise ExportError(("ZT1 " + self.port.readline().decode(
                "ascii", "replace")).strip())
        if prefix != MAGIC:
            raise ExportError(f"Bad packet magic: {prefix!r}")
        header_tail = self._exact(HEADER.size - 4)
        magic, actual_offset, actual_size, expected_crc = HEADER.unpack(prefix + header_tail)
        if magic != MAGIC or actual_offset != offset or actual_size != size:
            raise ExportError("Packet offset/length mismatch")
        payload = self._exact(size)
        actual_crc = zlib.crc32(payload) & 0xffffffff
        if actual_crc != expected_crc:
            raise ExportError("CRC32 mismatch in received data")
        return payload

    def close_image(self):
        self._send(b"CLOSE\n")
        if self._line() != "ZT1 CLOSED":
            raise ExportError("CLOSE not acknowledged")

    def quit(self):
        self._send(b"QUIT\n")
        if self._line() != "ZT1 BYE":
            raise ExportError("QUIT not acknowledged")


def safe_name(name):
    if not isinstance(name, str) or not (5 <= len(name) <= 120):
        return False
    if name[0] in "-." or not name.lower().endswith(".tap") or ".." in name:
        return False
    return all(c.isascii() and (c.isalnum() or c in "._-") for c in name)


def read_with_retries(client, offset, length, tries=3):
    # Deliberately do not retry transport timeouts. A delayed prior packet
    # could otherwise become the next reply on this stop-and-wait stream.
    for i in range(tries):
        try:
            return client.read(offset, length)
        except ExportError as exc:
            if "CRC32 mismatch" not in str(exc) or i == tries - 1:
                raise
    raise AssertionError("unreachable")


def verify_resume(client, part, progress_size, total):
    if progress_size > total:
        raise ExportError("Partial image larger than source")
    if not progress_size:
        return
    # Two separate windows catch common accidental rewrites/replacements.
    # This is not a cryptographic proof that every byte remained unchanged.
    positions = {0, max(0, progress_size - MAX_CHUNK)}
    with part.open("rb") as handle:
        for offset in sorted(positions):
            length = min(MAX_CHUNK, progress_size - offset)
            handle.seek(offset)
            old = handle.read(length)
            if client.read(offset, length) != old:
                raise ExportError("Resume fingerprint mismatch: source changed")


def copy_image(client, image, scsi_id, dest, resume=False, chunk_size=1024,
               progress=True):
    size = client.open(scsi_id, image)
    dest = Path(dest).expanduser()
    if dest.is_dir():
        dest = dest / image
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        raise ExportError(f"Destination already exists (will not overwrite): {dest}")

    partial = dest.with_name(dest.name + ".part")
    metadata = dest.with_name(dest.name + ".part.json")
    identity = {"protocol": "ZT1", "image": image, "scsi_id": scsi_id,
                "size": size}
    if partial.exists() or metadata.exists():
        if not resume or not partial.is_file() or not metadata.is_file():
            raise ExportError("Partial file present: use --resume or move .part and .part.json")
        if json.loads(metadata.read_text()) != identity:
            raise ExportError("Resume metadata mismatch; source or target differs")
        position = partial.stat().st_size
        verify_resume(client, partial, position, size)
    else:
        position = 0
        # Use exclusive create; no existing files are overwritten.
        with partial.open("xb"):
            pass
        with metadata.open("x") as f:
            json.dump(identity, f, sort_keys=True)

    start = time.monotonic()
    start_position = position
    with partial.open("r+b") as out:
        out.seek(position)
        while position < size:
            length = min(chunk_size, size - position)
            block = read_with_retries(client, position, length)
            out.write(block)
            position += len(block)
            if progress and (position == size or position % (1024 * 1024) < chunk_size):
                duration = max(time.monotonic() - start, 0.001)
                rate = (position - start_position) / duration
                print(f"\r{position:,} / {size:,} bytes ({position * 100 / max(size,1):.1f}%) "
                      f"transferred, {rate / 1024:.1f} KiB/s", end="", flush=True)
        out.flush()
        os.fsync(out.fileno())
    if progress:
        print()

    client.close_image()
    # Hash the actual complete file on disk, not only the USB data stream.
    sha = hashlib.sha256()
    with partial.open("rb") as inp:
        while True:
            block = inp.read(1024 * 1024)
            if not block:
                break
            sha.update(block)
    if partial.stat().st_size != size:
        raise ExportError("Downloaded size changed during verification")
    # Hardlink creation is atomic and EXCLUSIVE. os.rename() would
    # silently overwrite a destination created after our earlier check.
    os.link(partial, dest)
    partial.unlink()
    metadata.unlink()
    return dest, sha.hexdigest(), size


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="e.g. /dev/ttyACM0")
    parser.add_argument("--image", required=True, help="Tape basename, including .tap")
    parser.add_argument("--scsi-id", type=int, default=0, help="Tape SCSI ID (default: 0)")
    parser.add_argument("--output", required=True, help="Output filename or existing directory")
    parser.add_argument("--resume", action="store_true", help="Resume matching .part + .part.json")
    parser.add_argument("--chunk-size", type=int, default=MAX_CHUNK,
                        help="Max payload bytes per request (1..1024)")
    parser.add_argument("--timeout", type=int, default=120,
                        help="Per-response deadline in seconds")
    args = parser.parse_args(argv)
    if not 1 <= args.chunk_size <= MAX_CHUNK:
        parser.error("chunk-size must be 1..1024")
    if not safe_name(args.image):
        parser.error("image must be a safe .tap basename without paths")
    try:
        import serial  # pyserial required only for actual hardware
    except ImportError as exc:
        parser.error("pyserial is required: python3 -m pip install pyserial")
    client = None
    try:
        with serial.Serial(args.port, timeout=2, write_timeout=10, exclusive=True) as port:
            client = TapeClient(port, args.timeout)
            client.connect()
            try:
                dest, sha, size = copy_image(client, args.image, args.scsi_id,
                                             args.output, args.resume, args.chunk_size)
                print(f"Verified {size:,} bytes; SHA256 {sha}")
                print(f"Saved: {dest}")
            finally:
                try:
                    client.quit()
                except (ExportError, OSError, serial.SerialException):
                    pass
    except (OSError, ExportError, serial.SerialException, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
