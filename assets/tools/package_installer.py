"""Split the P4 merged image for updates that preserve NVS and PHY data.

Usage: python3 assets/tools/package_installer.py MERGED_IMAGE VERSION OUTPUT_DIR
The merged image remains available as a release download for manual installs.
"""
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]


def package(image_path, version, output_dir):
    image = Path(image_path).read_bytes()
    if len(image) <= 0x10000 or image[0x2000] != 0xE9 or image[0x10000] != 0xE9:
        raise ValueError("Expected a merged ESP32-P4 image with bootloader at 0x2000 and app at 0x10000")
    # Reject a different partition layout rather than overwriting saved data.
    partitions = []
    for pos in range(0x8000, 0x9000, 32):
        magic, kind, subtype, offset, size, name, flags = struct.unpack_from("<HBBII16sI", image, pos)
        if magic != 0x50AA:
            break
        partitions.append((name.rstrip(b"\0"), kind, subtype, offset, size))
    if not all(entry in partitions for entry in (
        (b"nvs", 1, 2, 0x9000, 0x6000),
        (b"phy_init", 1, 1, 0xF000, 0x1000),
        (b"factory", 0, 0, 0x10000, 0x800000),
    )) or len(image) > 0x810000:
        raise ValueError("Unexpected P4 partition layout or oversized application")

    manifest = json.loads((ROOT / "web/manifest.json").read_text())
    manifest["version"] = version
    parts = manifest["builds"][0]["parts"]
    expected = [("bootloader.bin", 0x2000), ("partition-table.bin", 0x8000), ("esp32_vag_scanner.bin", 0x10000)]
    if [(part["path"], part["offset"]) for part in parts] != expected:
        raise ValueError("Installer manifest does not match the P4 firmware layout")
    output = Path(output_dir)
    output.mkdir(parents=True, exist_ok=True)
    for (name, start), end in zip(expected, (0x8000, 0x9000, len(image))):
        (output / name).write_bytes(image[start:end])
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    package(*sys.argv[1:])
