"""Verify installer updates exclude the saved-settings partitions."""
import json
from pathlib import Path
import struct
import tempfile
import unittest

from package_installer import package


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.image_path = self.root / "merged.bin"
        self.output = self.root / "site"
        self.image = bytearray(b"\xff" * 0x11000)
        self.image[0x2000] = self.image[0x10000] = 0xE9
        for index, entry in enumerate((
            (1, 2, 0x9000, 0x6000, b"nvs"),
            (1, 1, 0xF000, 0x1000, b"phy_init"),
            (0, 0, 0x10000, 0x800000, b"factory"),
        )):
            struct.pack_into("<HBBII16sI", self.image, 0x8000 + index * 32, 0x50AA, *entry, 0)

    def test_parts_match_firmware_and_do_not_write_saved_settings(self):
        marker = b"saved-settings-must-survive"
        self.image[0x9000:0x9000 + len(marker)] = marker
        self.image_path.write_bytes(self.image)
        package(self.image_path, "v1.1.0", self.output)
        manifest = json.loads((self.output / "manifest.json").read_text())
        self.assertEqual(manifest["version"], "v1.1.0")
        self.assertFalse(manifest["new_install_prompt_erase"])
        parts = manifest["builds"][0]["parts"]
        self.assertEqual(len(parts), 3)
        for part in parts:
            data = (self.output / part["path"]).read_bytes()
            start = part["offset"]
            end = start + len(data)
            self.assertEqual(data, self.image[start:end])
            self.assertTrue(end <= 0x9000 or start >= 0x10000)
            self.assertNotIn(marker, data)

    def test_rejects_changed_partition_layout_before_writing_files(self):
        struct.pack_into("<I", self.image, 0x8000 + 4, 0xA000)
        self.image_path.write_bytes(self.image)
        with self.assertRaises(ValueError):
            package(self.image_path, "v1.1.0", self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_non_firmware_image(self):
        self.image_path.write_bytes(b"not firmware")
        with self.assertRaises(ValueError):
            package(self.image_path, "v1.1.0", self.output)


if __name__ == "__main__":
    unittest.main()
