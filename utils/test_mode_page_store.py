#!/usr/bin/env python3
"""Build and test the exact platform-independent MODE persistence firmware code."""
from pathlib import Path
import subprocess
import tempfile
import os

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="zuluscsi-flash-mode-") as tmp:
    binary = Path(tmp) / "regression"
    subprocess.run([
        os.getenv("CXX","g++"),"-std=c++11","-O2","-Wall","-Wextra",
        "-Werror","-UNDEBUG","-I",str(root / "src"),
        str(root / "src/mode_page_store.cpp"),
        str(root / "tests/mode_page_store_test.cpp"),
        "-o",str(binary)
    ],check=True)
    subprocess.run([str(binary)],check=True)
