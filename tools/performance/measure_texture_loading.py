# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""生成真实纹理负载，重复测量同步、线程池和协作模式的渲染帧完成间隔。"""

import argparse
import csv
import json
from pathlib import Path
import statistics
import struct
import subprocess
import zlib


def png(path, width, color):
    def chunk(name, payload):
        return struct.pack(">I", len(payload)) + name + payload + struct.pack(">I", zlib.crc32(name + payload))
    row = b"\0" + bytes(color) * width
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", width, width, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(row * width, 1))
    data += chunk(b"IEND", b"")
    path.write_bytes(data)


def fixture(root):
    for directory in ("textures", "materials", "models", "scenes"):
        (root / directory).mkdir(parents=True, exist_ok=True)
    png(root / "textures/before.png", 8, (20, 60, 220, 255))
    png(root / "textures/after.png", 4096, (230, 70, 20, 255))
    def write(path, value):
        (root / path).write_text(json.dumps(value), encoding="utf-8")
    write("materials/test.material.json", {
        "format": "gneiss.material", "version": 3, "color": [1, 1, 1, 1],
        "base_color_texture": "asset://textures/test.texture.json", "metallic": 0, "roughness": 1})
    write("models/test.mesh.json", {
        "format": "gneiss.mesh", "version": 3, "topology": "triangle_list",
        "vertices": [[-.8, -.7, 0], [.8, -.7, 0], [0, .8, 0]],
        "uvs": [[0, 0], [1, 0], [.5, 1]], "normals": [[0, 0, 1]] * 3})
    write("scenes/test.scene.json", {
        "format": "gneiss.scene", "version": 4,
        "scene_uuid": "00000000-0000-4000-8000-000000000001",
        "objects": [
            {"uuid": "00000000-0000-4000-8000-000000000002", "name": "Camera", "parent": None,
             "transform": {"translation": [0, 0, 2], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
             "components": {"camera": {"vertical_field_of_view_radians": 1.04719755,
                                        "near_plane": .1, "far_plane": 100, "is_primary": True}}},
            {"uuid": "00000000-0000-4000-8000-000000000003", "name": "Triangle", "parent": None,
             "transform": {"translation": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
             "components": {"mesh_renderer": {"mesh": "asset://models/test.mesh.json",
                                                "material": "asset://materials/test.material.json"}}}],
        "prefab_instances": []})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--samples", type=int, default=3)
    args = parser.parse_args()
    output = args.output.resolve()
    root = output / "assets"
    fixture(root)
    results = []
    for mode in ("sync", "thread", "cooperative"):
        for index in range(args.samples):
            (root / "textures/test.texture.json").write_text(json.dumps({
                "format": "gneiss.texture", "version": 1,
                "source": "asset://textures/before.png", "color_space": "srgb"}), encoding="utf-8")
            sample = output / f"{mode}-{index}.csv"
            process = subprocess.run([str(args.executable.resolve()), str(root), mode, str(sample)],
                                     capture_output=True, timeout=45)
            text = process.stdout.decode("utf-8", errors="replace")
            (output / f"{mode}-{index}.log").write_bytes(process.stdout + process.stderr)
            if process.returncode != 0:
                raise RuntimeError(f"{mode} 运行失败：{process.returncode}; {text}")
            rows = list(csv.DictReader(sample.open(encoding="utf-8")))
            # 统计真实执行帧的完成间隔；采样是宿主观察到的新完成帧，不包括被队列替换的帧。
            measured = [float(row["render_interval_ms"]) for row in rows if float(row["elapsed"]) >= 1]
            measured.sort()
            result = {"mode": mode, "sample": index, "frames_observed": len(measured),
                      "render_interval_median_ms": statistics.median(measured),
                      "render_interval_p95_ms": measured[int((len(measured) - 1) * .95)],
                      "render_interval_max_ms": max(measured), "details": text.strip()}
            results.append(result)
            print(json.dumps(result, ensure_ascii=False), flush=True)
    (output / "summary.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
