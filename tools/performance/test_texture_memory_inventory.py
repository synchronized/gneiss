# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""验证非整块、非方形 Mip 计算及损坏输入，避免低估压缩纹理容量。"""

from pathlib import Path
import struct
import tempfile
import unittest
import zlib

import texture_memory_inventory as tool


class InventoryTest(unittest.TestCase):
    def test_small_and_non_square_mips(self):
        self.assertEqual(tool.mip_sizes(1, 1), (1, 4, 16))
        self.assertEqual(tool.mip_sizes(5, 3), (3, 72, 64))
        self.assertEqual(tool.mip_sizes(1, 8), (4, 60, 80))
        with self.assertRaises(ValueError):
            tool.mip_sizes(0, 1)

    def test_inventory_and_invalid_header(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                tool.inventory(root)
            data = b"IHDR" + struct.pack(">IIBBBBB", 5, 3, 8, 6, 0, 0, 0)
            header = b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + data
            header += struct.pack(">I", zlib.crc32(data))
            path = root / "test.png"
            path.write_bytes(header)
            report = tool.inventory(root)
            self.assertEqual(report["texture_count"], 1)
            self.assertEqual(report["totals"]["dual_variant_payload_bytes"], 136)
            self.assertEqual(report["textures"][0]["path"], "test.png")
            path.write_bytes(header[:-1] + bytes([header[-1] ^ 1]))
            with self.assertRaises(ValueError):
                tool.inventory(root)
            path.write_bytes(header[:20])
            with self.assertRaises(ValueError):
                tool.inventory(root)


if __name__ == "__main__":
    unittest.main()
