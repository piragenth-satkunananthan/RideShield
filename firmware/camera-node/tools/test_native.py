#!/usr/bin/env python3
"""Host regression checks for code shared with both camera firmware profiles."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="rideshield-camera-tests-") as tmp:
    exe = str(Path(tmp) / "test_stream")
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "include"), str(root / "tests/test_stream.cpp"),
                    "-o", exe], check=True)
    subprocess.run([exe], check=True)
