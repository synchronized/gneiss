# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""导入已经审计的 Sponza 配置，恢复固定相机，核对几何实例数量并记录转换耗时。"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

from prepare_sponza import ENTRY, LICENSE


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("assetc", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source_root = args.source.resolve()
    output = args.output.resolve()
    if output.exists():
        raise ValueError("输出目录必须不存在，避免 assetc 覆盖已有资产")
    audit = json.loads((source_root / "audit.json").read_text(encoding="utf-8"))
    if audit["profile"] not in ("daily", "full") or not audit.get("members"):
        raise ValueError("需要完整准备的 daily/full 配置")
    for member in audit["members"]:
        path = (source_root / member["path"]).resolve()
        if not path.is_relative_to(source_root):
            raise ValueError("清单路径逃逸")
        with path.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != member["output_sha256"]:
                raise ValueError(f"输入校验失败：{path}")
    source = json.loads((source_root / ENTRY).read_text(encoding="utf-8"))
    start = time.monotonic()
    process = subprocess.run([str(args.assetc.resolve()), "import", str(source_root / ENTRY),
                              "--output", str(output)], capture_output=True, timeout=900)
    import_ms = (time.monotonic() - start) * 1000
    if process.returncode:
        raise RuntimeError(process.stdout.decode("utf-8", errors="replace") +
                           process.stderr.decode("utf-8", errors="replace"))
    scene_path = output / "scenes/scene.scene.json"
    scene = json.loads(scene_path.read_text(encoding="utf-8"))
    expected_renderers = sum(len(source["meshes"][n["mesh"]]["primitives"])
                             for n in source["nodes"] if "mesh" in n)
    actual_renderers = sum("mesh_renderer" in n["components"] for n in scene["objects"])
    if actual_renderers != expected_renderers:
        raise RuntimeError(f"实例数量不一致：{actual_renderers} != {expected_renderers}")
    camera_index = next(i for i, n in enumerate(source["nodes"]) if n.get("camera") == 0)
    camera_uuid = f"00000000-0000-4000-8000-{camera_index + 1:012x}"
    camera = next(n for n in scene["objects"] if n["uuid"] == camera_uuid)
    projection = source["cameras"][0]["perspective"]
    camera["components"]["camera"] = {
        "vertical_field_of_view_radians": projection["yfov"], "near_plane": projection["znear"],
        "far_plane": projection["zfar"], "is_primary": True,
    }
    scene_path.write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8")
    shutil.copyfile(source_root / LICENSE, output / "credits_license.txt")
    report = {"profile": audit["profile"], "archive_sha256": audit["archive_sha256"],
              "assetc_sha256": hashlib.sha256(args.assetc.read_bytes()).hexdigest(),
              "import_ms": import_ms, "nodes": len(scene["objects"]),
              "renderers": actual_renderers, "camera_source_node": camera_index,
              "camera_uuid": camera_uuid, "camera_transform": camera["transform"],
              "source_audit_rendering_differences": audit["rendering_differences"],
              "import_diagnostics": process.stderr.decode("utf-8", errors="replace"),
              "rendering_differences": [
                  "保留五类 PBR 贴图、切线、UV1、顶点色、Alpha 和双面状态；仍需实景 GPU 验收。",
                  "KHR_lights_punctual 灯光未映射；本工具显式恢复固定测试相机。",
                  "透明为对象级排序，不保证相交三角形正确顺序；不含折射/OIT。"],
              "mesh_files": len(list((output / "models").glob("*.gneiss-mesh"))),
              "runtime_bytes": sum(p.stat().st_size for p in output.rglob("*") if p.is_file())}
    (output / "conversion.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                                            encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
