# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""统计目录内 PNG 身份与理论纹理负载；不解码，不代表运行时或驱动驻留测量。"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def mip_sizes(width, height):
    """完整二维 Mip 链；BC7 每个至少 4×4 的块占 16 字节。"""
    if width <= 0 or height <= 0:
        raise ValueError("纹理尺寸必须为正")
    levels = rgba8 = bc7 = 0
    while True:
        levels += 1
        rgba8 += width * height * 4
        bc7 += ((width + 3) // 4) * ((height + 3) // 4) * 16
        if width == height == 1:
            return levels, rgba8, bc7
        width, height = max(1, width // 2), max(1, height // 2)


def inspect_png(path):
    with path.open("rb") as stream:
        header = stream.read(33)
        if (len(header) != 33 or header[:8] != b"\x89PNG\r\n\x1a\n"
                or header[8:16] != b"\0\0\0\rIHDR"
                or zlib.crc32(header[12:29]) != struct.unpack(">I", header[29:33])[0]):
            raise ValueError(f"PNG IHDR 无效: {path}")
        width, height = struct.unpack(">II", header[16:24])
        levels, rgba8, bc7 = mip_sizes(width, height)
        stream.seek(0)
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {
        "sha256": digest, "file_bytes": path.stat().st_size,
        "width": width, "height": height, "mip_levels": levels,
        "rgba8_base_bytes": width * height * 4,
        "rgba8_full_mips_bytes": rgba8, "bc7_full_mips_bytes": bc7,
        "dual_variant_payload_bytes": rgba8 + bc7,
    }


def inventory(root):
    root = root.resolve(strict=True)
    textures = []
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix.lower() == ".png":
            if not path.resolve().is_relative_to(root):
                raise ValueError(f"文件链接逃逸统计目录: {path}")
            textures.append({"path": path.relative_to(root).as_posix(), **inspect_png(path)})
    if not textures:
        raise ValueError("目录中没有 PNG，不能生成空的成功报告")
    fields = ("file_bytes", "rgba8_base_bytes", "rgba8_full_mips_bytes",
              "bc7_full_mips_bytes", "dual_variant_payload_bytes")
    return {
        "schema": 1,
        "scope": "目录内全部 PNG；按文件计数，不代表场景依赖去重后的纹理实例",
        "method": "校验 IHDR 并分块哈希；未解码或校验完整 PNG；负载为尺寸公式估算",
        "excluded": ["容器与对齐", "网格与材质", "CPU 副本", "GPU 分配与驱动驻留"],
        "texture_count": len(textures),
        "totals": {field: sum(item[field] for item in textures) for field in fields},
        "textures": textures,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = inventory(args.root)
    # 不覆盖已有测量，避免丢失不同输入的证据。
    with args.output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print(json.dumps(report["totals"]))


if __name__ == "__main__":
    main()
