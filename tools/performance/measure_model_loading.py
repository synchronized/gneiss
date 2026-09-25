# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""重新导入真实 Lantern GLB，测量其混合资产加载和窗口交互；不把导入耗时算作加载耗时。"""

import argparse
import csv
import json
from pathlib import Path
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("benchmark", type=Path)
    parser.add_argument("assetc", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--samples", type=int, default=3)
    args = parser.parse_args()
    root = args.output.resolve() / "assets"
    root.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    subprocess.run([str(args.assetc.resolve()), "import", str(args.source.resolve()),
                    "--output", str(root)], check=True, timeout=60)
    import_ms = (time.monotonic() - start) * 1000
    camera = {"uuid": "00000000-0000-4000-8000-000000000002", "parent": None,
              "transform": {"translation": [0, 0, 2], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
              "components": {"camera": {"vertical_field_of_view_radians": 1.04719755,
                                         "near_plane": .1, "far_plane": 100, "is_primary": True}}}
    objects = [camera]
    for index, mesh in enumerate(sorted((root / "models").glob("*.gneiss-mesh"))):
        objects.append({"uuid": f"00000000-0000-4000-8000-{index+3:012d}", "parent": None,
                        "transform": {"translation": [0, 0, 0], "rotation": [0, 0, 0, 1],
                                      "scale": [.12, .12, .12]},
                        "components": {"mesh_renderer": {"mesh": "asset://models/" + mesh.name,
                            "material": "asset://materials/material-0.material.json"}}})
    (root / "scenes/test.scene.json").write_text(json.dumps({
        "format": "gneiss.scene", "version": 4,
        "scene_uuid": "00000000-0000-4000-8000-000000000001",
        "objects": objects, "prefab_instances": []}), encoding="utf-8")
    results = []
    for mode in ("thread", "cooperative", "close"):
        for index in range(1 if mode == "close" else args.samples):
            sample = args.output.resolve() / f"{mode}-{index}.csv"
            process = subprocess.run([str(args.benchmark.resolve()), str(root), mode,
                                      str(sample), "model"], capture_output=True, timeout=45)
            (sample.with_suffix(".log")).write_bytes(process.stdout + process.stderr)
            if process.returncode:
                raise RuntimeError(f"{mode}: {process.returncode}: {process.stderr!r}")
            with sample.open(encoding="utf-8") as stream:
                measured = sorted(float(row["render_interval_ms"]) for row in csv.DictReader(stream)
                                  if float(row["elapsed"]) >= 1)
            result = {"mode": mode, "sample": index, "import_ms": import_ms,
                      "render_interval_median_ms": statistics.median(measured) if measured else None,
                      "render_interval_p95_ms": measured[int((len(measured)-1)*.95)] if measured else None,
                      "render_interval_max_ms": max(measured) if measured else None,
                      "details": process.stdout.decode("utf-8", errors="replace").strip()}
            results.append(result)
            print(json.dumps(result, ensure_ascii=False), flush=True)
    (args.output / "summary.json").write_text(json.dumps(results, ensure_ascii=False, indent=2),
                                             encoding="utf-8")


if __name__ == "__main__":
    main()
