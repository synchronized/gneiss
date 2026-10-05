# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""审计 Intel Sponza 固定基础包，并生成保留层级和几何的外部测试输入。"""

import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import struct
import zipfile


ENTRY = "main_sponza/NewSponza_Main_glTF_003.gltf"
LICENSE = "main_sponza/credits_license.txt"
IDENTITIES = {
    ENTRY: "e04c4c540c74bdddcbd3f590a85c14119bfd5839702246a23b3a737c0cce2400",
    LICENSE: "3f981e31f8de04f3754e63d7c259a0a78f71db8f01b5155c58cac52fb79cc2b9",
}
ARCHIVE_SHA256 = "b8bb853884ab1566b3beb35666bd09882a4e0dc16661e4684e103792cf0229b9"
SOURCE = "https://cdrdv2.intel.com/v1/dl/getContent/830833"


def safe_member(uri):
    """本夹具只有相对文件 URI；拒绝 URL、转义和平台路径。"""
    path = PurePosixPath(uri)
    if (not uri or path.is_absolute() or any(p in ("..", ".") for p in uri.split("/"))
            or any(c in uri for c in ("\\", ":", "%", "?", "#"))):
        raise ValueError(f"不安全的依赖 URI：{uri}")
    return "main_sponza/" + uri


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def audit(archive):
    """只读 ZIP；输出资产规模、引用和当前导入/渲染能力的差异。"""
    for name, expected in IDENTITIES.items():
        if sha256(archive.read(name)) != expected:
            raise ValueError(f"固定夹具身份不匹配：{name}")
    source = json.loads(archive.read(ENTRY))
    primitives = [p for mesh in source["meshes"] for p in mesh["primitives"]]
    if source.get("extensionsRequired") or source.get("skins") or source.get("animations"):
        raise ValueError("夹具出现未支持的必需扩展、蒙皮或动画")
    for primitive in primitives:
        if primitive.get("mode", 4) != 4 or not {"POSITION", "NORMAL", "TEXCOORD_0"}.issubset(
                primitive["attributes"]):
            raise ValueError("夹具 Primitive 不符合当前导入范围")
    image_rows = []
    for image in source["images"]:
        name = safe_member(image["uri"])
        with archive.open(name) as stream:
            header = stream.read(24)
        if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
            raise ValueError(f"不是 PNG：{name}")
        width, height = struct.unpack(">II", header[16:24])
        image_rows.append({"uri": image["uri"], "width": width, "height": height,
                           "rgba8_bytes": width * height * 4})
    materials = source["materials"]
    base_images = sorted({source["textures"][m["pbrMetallicRoughness"]["baseColorTexture"]["index"]]
                          ["source"] for m in materials
                          if "baseColorTexture" in m.get("pbrMetallicRoughness", {})})
    mesh_triangles = [sum(source["accessors"][p["indices"]]["count"] // 3
                         if "indices" in p else
                         source["accessors"][p["attributes"]["POSITION"]]["count"] // 3
                         for p in mesh["primitives"]) for mesh in source["meshes"]]
    return source, {
        "source_url": SOURCE, "entry": ENTRY, "source_identities": IDENTITIES,
        "nodes": len(source["nodes"]), "meshes": len(source["meshes"]),
        "primitives": len(primitives), "materials": len(materials),
        "unique_triangles": sum(mesh_triangles),
        "instanced_triangles": sum(mesh_triangles[n["mesh"]] for n in source["nodes"] if "mesh" in n),
        "extensions_used": source.get("extensionsUsed", []), "images": image_rows,
        "nonopaque_materials": [{"name": m.get("name"), "alpha_mode": m.get("alphaMode")}
                                for m in materials if m.get("alphaMode", "OPAQUE") != "OPAQUE"],
        "base_color_images": base_images,
        "base_color_rgba8_bytes": sum(image_rows[i]["rgba8_bytes"] for i in base_images),
        "all_images_rgba8_bytes": sum(i["rgba8_bytes"] for i in image_rows),
        "rendering_differences": [
            "当前路径只采样基础颜色贴图；法线和金属度/粗糙度贴图不参与最终着色。",
            "KHR_lights_punctual 灯光不导入为引擎灯光；不能作为完整 PBR 效果对比。",
            "TANGENT、TEXCOORD_1 和 COLOR_0 不进入当前顶点布局。",
            "导入器保留节点变换但不生成 glTF 相机组件；测试相机须显式配置并记录。",
            "glTF alphaMode 不映射到当前材质管线，BLEND 贴花不保证原始透明效果。",
        ],
        "license_note": "保留原始 credits_license.txt；其中既有用途说明也有 CC BY 4.0 正文。"
                        "本工具不重新解释许可，也不将资产纳入源码发行包。",
    }


def prepare(archive_path, output, profile):
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError("输出目录须为空，防止混入旧配置或覆盖已有输入")
    output.mkdir(parents=True, exist_ok=True)
    with archive_path.open("rb") as stream:
        archive_hash = hashlib.file_digest(stream, "sha256").hexdigest()
    if archive_hash != ARCHIVE_SHA256:
        raise ValueError("固定 ZIP 夹具身份不匹配")
    with zipfile.ZipFile(archive_path) as archive:
        source, report = audit(archive)
        report.update({"archive_sha256": archive_hash, "archive_bytes": archive_path.stat().st_size,
                       "profile": profile, "daily_max_dimension": 1024 if profile == "daily" else None})
        if profile != "audit":
            # 只提取 glTF 的依赖，保留全部图片，避免改变源描述的引用关系。
            members = {ENTRY, LICENSE}
            members.update(safe_member(x["uri"]) for x in source["images"] + source["buffers"])
            report["members"] = []
            for name in sorted(members):
                data = archive.read(name)  # 完整读取会校验 ZIP CRC。
                original_hash = sha256(data)
                if profile == "daily" and name.lower().endswith(".png"):
                    from PIL import Image, __version__ as pillow_version
                    report["pillow_version"] = pillow_version
                    with Image.open(io.BytesIO(data)) as image:
                        image.thumbnail((1024, 1024), Image.Resampling.LANCZOS)
                        encoded = io.BytesIO()
                        image.save(encoded, format="PNG")
                        data = encoded.getvalue()
                target = (output / name).resolve()
                if not target.is_relative_to(output):
                    raise ValueError("输出路径逃逸")
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                report["members"].append({"path": name, "source_sha256": original_hash,
                                          "output_sha256": sha256(data), "output_bytes": len(data)})
        (output / "audit.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--profile", choices=("audit", "daily", "full"), default="audit")
    args = parser.parse_args()
    report = prepare(args.archive.resolve(), args.output, args.profile)
    print(json.dumps({k: report[k] for k in ("nodes", "primitives", "instanced_triangles",
                                           "base_color_rgba8_bytes", "archive_sha256")}, indent=2))


if __name__ == "__main__":
    main()
