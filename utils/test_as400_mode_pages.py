#!/usr/bin/env python3
"""Compile and execute the actual firmware MODE-page model on the host."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="as400-mode-pages-") as tmp:
    exe = Path(tmp) / "mode_pages_test"
    subprocess.run([
        os.getenv("CXX", "g++"), "-std=c++11", "-O2", "-Wall", "-Wextra",
        "-Werror", "-UNDEBUG", "-I", str(root / "src"),
        str(root / "src/as400_mode_pages.cpp"),
        str(root / "tests/as400_mode_pages_test.cpp"),
        "-o", str(exe)
    ], check=True)
    subprocess.run([str(exe)], check=True)
