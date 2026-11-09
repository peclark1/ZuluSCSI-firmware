#!/usr/bin/env python3
"""No-device regression tests for the stop-and-wait ZT1 download client."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from utils.zuluscsi_tape_fetch import (
    ExportError, TapeClient, copy_image, read_with_retries, safe_name,
)


class FakeSerial:
    def __init__(self, image):
        self.image = image
        self.incoming = bytearray()
        self.commands = bytearray()
        self.in_protocol = False
        self.armed = False
        self.closed = False
        self.bad_crc_once = False
        self.read_count = 0

    def reset_input_buffer(self):
        self.incoming.clear()

    def write(self, data):
        if not self.in_protocol:
            if not self.armed and data == b"f":
                self.armed = True
                self.incoming.extend(b"[Boot] ZT1 ARMED: send HELLO ZT1\n")
                return len(data)
            if self.armed and data == b"HELLO ZT1\n":
                self.in_protocol = True
                self.incoming.extend(b"ZT1 READY\n")
                return len(data)
            raise AssertionError("handshake missing or invalid: " + repr(data))
        self.commands.extend(data)
        while b"\n" in self.commands:
            line, _, rest = self.commands.partition(b"\n")
            self.commands = bytearray(rest)
            parts = line.decode("ascii").split()
            if parts[0] == "OPEN":
                self.incoming.extend(f"ZT1 SIZE {len(self.image)}\n".encode())
            elif parts[0] == "READ":
                pos, length = int(parts[1]), int(parts[2])
                block = self.image[pos:pos + length]
                assert len(block) == length
                crc = zlib.crc32(block) & 0xffffffff
                if self.bad_crc_once:
                    crc ^= 1
                    self.bad_crc_once = False
                self.read_count += 1
                self.incoming.extend(struct.pack("<4sQHI",
                                                 b"ZT1D", pos, length, crc))
                self.incoming.extend(block)
            elif parts[0] == "CLOSE":
                self.incoming.extend(b"ZT1 CLOSED\n")
            elif parts[0] == "QUIT":
                self.incoming.extend(b"ZT1 BYE\n")
            else:
                self.incoming.extend(b"ZT1 ERR BAD_REQUEST\n")
        return len(data)

    def read(self, n):
        # Simulate packet fragmentation over multiple USB CDC polls.
        count = min(n, len(self.incoming), 31)
        data = bytes(self.incoming[:count])
        del self.incoming[:count]
        return data

    def readline(self):
        end = self.incoming.find(b"\n")
        if end == -1:
            return b""
        return self.read(end + 1) if end < 30 else self._long_line(end + 1)

    def _long_line(self, size):
        data = bytes(self.incoming[:size])
        del self.incoming[:size]
        return data

    def flush(self):
        pass


class TapeExportTests(unittest.TestCase):
    def connect(self, image):
        port = FakeSerial(image)
        client = TapeClient(port, timeout=0.1)
        client.connect()
        return client, port

    def test_names(self):
        self.assertTrue(safe_name("v2r1-dist-tape-created-on-b10.tap"))
        for bad in ("../etc/passwd", ".hidden.tap", "foo..tap",
                    "space name.tap", "tape.bin", "/absolute.tap"):
            self.assertFalse(safe_name(bad))

    def test_packet_and_crc_retry(self):
        client, port = self.connect(b"abcdefghijklmnopqrstuvwxy")
        self.assertEqual(client.open(0, "demo.tap"), 25)
        port.bad_crc_once = True
        self.assertEqual(read_with_retries(client, 3, 11), b"defghijklmn")
        self.assertEqual(port.read_count, 2)
        client.close_image()
        client.quit()

    def test_complete_and_resume(self):
        data = bytes(range(256)) * 6 + b"last!"
        with tempfile.TemporaryDirectory() as td:
            dest = Path(td) / "archive.tap"
            partial = Path(td) / "archive.tap.part"
            metadata = Path(td) / "archive.tap.part.json"

            partial.write_bytes(data[:61])
            metadata.write_text(json.dumps(
                {"protocol": "ZT1", "image": "archive.tap",
                 "scsi_id": 0, "size": len(data)}))
            client, port = self.connect(data)
            output, digest, size = copy_image(
                client, "archive.tap", 0, dest, resume=True, chunk_size=37,
                progress=False)
            self.assertEqual(output, dest)
            self.assertEqual(output.read_bytes(), data)
            self.assertEqual(size, len(data))
            self.assertEqual(len(digest), 64)
            self.assertFalse(partial.exists())
            self.assertFalse(metadata.exists())
            client.quit()

            client2, _ = self.connect(data)
            with self.assertRaises(ExportError):
                copy_image(client2, "archive.tap", 0, dest, progress=False)

    def test_opt_in_4096_chunk_round_trip(self):
        # Exercise packet assembly even when USB fragments into 31-byte
        # reads, verifying that larger reads preserve exact byte ordering.
        data = bytes(range(256)) * 40 + b"end"
        with tempfile.TemporaryDirectory() as td:
            client, port = self.connect(data)
            dest, digest, size = copy_image(
                client, "large.tap", 0, Path(td) / "large.tap",
                chunk_size=4096, progress=False)
            self.assertEqual(size, len(data))
            self.assertEqual(dest.read_bytes(), data)
            self.assertEqual(port.read_count, 3)
            self.assertEqual(len(digest), 64)
            client.quit()

    def test_mismatch_resume_refused(self):
        data = b"123456789" * 10
        with tempfile.TemporaryDirectory() as td:
            dest = Path(td) / "archive.tap"
            Path(td, "archive.tap.part").write_bytes(b"BAD" + data[3:30])
            Path(td, "archive.tap.part.json").write_text(json.dumps(
                {"protocol": "ZT1", "image": "archive.tap",
                 "scsi_id": 0, "size": len(data)}))
            client, _ = self.connect(data)
            with self.assertRaisesRegex(ExportError, "fingerprint mismatch"):
                copy_image(client, "archive.tap", 0, dest, resume=True,
                           progress=False)

    def test_empty(self):
        with tempfile.TemporaryDirectory() as td:
            client, _ = self.connect(b"")
            dest, digest, size = copy_image(client, "empty.tap", 0,
                                            Path(td) / "empty.tap",
                                            progress=False)
            self.assertEqual(size, 0)
            self.assertEqual(dest.read_bytes(), b"")
            self.assertEqual(len(digest), 64)


if __name__ == "__main__":
    unittest.main()
