# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""用原创微型 ZIP 验证夹具身份、引用、输出隔离和报告，不下载第三方资产。"""

import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import prepare_sponza as fixture


class FixtureTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = {
            "nodes": [{"mesh": 0}, {"mesh": 0}],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 0, "TEXCOORD_0": 0},
                                          "indices": 0}]}],
            "accessors": [{"count": 3}],
            "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
            "textures": [{"source": 0}], "images": [{"uri": "textures/test.png"}],
            "buffers": [{"uri": "test.bin", "byteLength": 3}],
        }

    def tearDown(self):
        self.temporary.cleanup()

    def archive(self):
        contents = {
            fixture.ENTRY: json.dumps(self.source).encode(), fixture.LICENSE: b"Original test fixture",
            "main_sponza/textures/test.png": b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) +
                                            b"IHDR" + struct.pack(">II", 2, 3),
            "main_sponza/test.bin": b"abc",
        }
        path = self.root / "fixture.zip"
        with zipfile.ZipFile(path, "w") as archive:
            for name, data in contents.items():
                archive.writestr(name, data)
        identities = {name: hashlib.sha256(contents[name]).hexdigest() for name in fixture.IDENTITIES}
        return path, identities

    def test_report_and_full_copy(self):
        path, identities = self.archive()
        with patch.object(fixture, "IDENTITIES", identities), patch.object(
                fixture, "ARCHIVE_SHA256", hashlib.sha256(path.read_bytes()).hexdigest()):
            report = fixture.prepare(path, self.root / "full", "full")
            self.assertEqual((report["nodes"], report["primitives"], report["instanced_triangles"]), (2, 1, 2))
            self.assertEqual(report["base_color_rgba8_bytes"], 24)
            self.assertEqual(len(report["members"]), 4)
            self.assertEqual((self.root / "full/main_sponza/test.bin").read_bytes(), b"abc")
            with self.assertRaisesRegex(ValueError, "输出目录须为空"):
                fixture.prepare(path, self.root / "full", "full")

    def test_identity_mismatch(self):
        path, _ = self.archive()
        with self.assertRaisesRegex(ValueError, "身份不匹配"):
            fixture.prepare(path, self.root / "audit", "audit")

    def test_path_escape(self):
        for uri in ("../test.bin", "/test.bin", "C:/test.bin", "https://host/x", "x\\y", "%2e%2e/x"):
            with self.subTest(uri=uri), self.assertRaises(ValueError):
                fixture.safe_member(uri)

    def test_unsupported_scope(self):
        self.source["extensionsRequired"] = ["TEST_required"]
        path, identities = self.archive()
        with patch.object(fixture, "IDENTITIES", identities), patch.object(
                fixture, "ARCHIVE_SHA256", hashlib.sha256(path.read_bytes()).hexdigest()), \
                self.assertRaisesRegex(ValueError, "未支持"):
            fixture.prepare(path, self.root / "audit", "audit")


if __name__ == "__main__":
    unittest.main()
