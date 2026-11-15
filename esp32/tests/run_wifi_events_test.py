"""Exercise the factory C6's duplicate station-start regression on the host."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="vag-wifi-events-") as directory:
    binary = str(Path(directory) / "test_wifi_events")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
        "-I", str(root / "esp32/tests/wifi_stubs"),
        "-I", str(root / "esp32/tests/stubs"),
        str(root / "esp32/tests/test_wifi_events.c"),
        str(root / "esp32/main/wifi_events.c"), "-o", binary,
    ], check=True)
    subprocess.run([binary], check=True)
