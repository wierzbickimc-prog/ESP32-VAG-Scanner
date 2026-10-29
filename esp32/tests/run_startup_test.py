"""Build and run the startup regression test without ESP32 hardware or ESP-IDF."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="vag-startup-") as directory:
    binary = str(Path(directory) / "test_startup")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=gnu99", "-Wall", "-Wextra", "-Werror",
        "-I", str(root / "esp32/tests/stubs"), "-I", str(root / "kwcore"),
        str(root / "esp32/tests/test_startup.c"),
        str(root / "kwcore/labels.c"), str(root / "kwcore/kw1281.c"),
        str(root / "kwcore/fault_table.c"), "-o", binary,
    ], check=True)
    subprocess.run([binary], check=True)
