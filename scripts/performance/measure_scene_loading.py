# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""顺序运行 Sponza 首载矩阵，保存原始样本、图像摘要和固定门槛的检查结果。"""

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import platform
import subprocess
import time

from prepare_sponza import ARCHIVE_SHA256


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def observed_frames(path):
    """主线程仅采样最新间隔，不能据此还原所有已呈现帧。"""
    with path.open(encoding="utf-8", newline="") as stream:
        values = [float(row["latest_interval_ms"]) for row in csv.DictReader(stream)]
    values = [value for value in values if value > 0]
    return {"count": len(values), "p50_ms": percentile(values, .5) if values else 0,
            "p95_ms": percentile(values, .95) if values else 0,
            "max_ms": max(values, default=0)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cache", type=Path)
    parser.add_argument("--debug", type=Path, help="可选的 Debug 功能矩阵；性能门槛仅检查 Release")
    parser.add_argument("--release", type=Path, required=True)
    parser.add_argument("--daily-assets", type=Path)
    parser.add_argument("--full-assets", type=Path)
    parser.add_argument("--expected-resources", type=int, default=505)
    parser.add_argument("--baseline", type=Path, help="0.49 基线原始报告，用于吞吐、内存和图像比较")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    configurations = ("release", "debug") if args.debug else ("release",)
    baseline = json.loads(args.baseline.read_text(encoding="utf-8")) if args.baseline else None
    # 不复用旧结果，防止不同二进制或不同资产的样本混为一次验收。
    args.output.mkdir(parents=True, exist_ok=False)
    samples = []
    failures = []
    report = {
        "platform": platform.platform(),
        "cache_policy": "每样本独立进程，不清理操作系统文件缓存；无并行样本",
        "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "status": "running",
        "executables": {},
        "assets": {},
        "samples": samples,
        "failures": failures,
    }
    for configuration in configurations:
        executable = getattr(args, configuration).resolve()
        report["executables"][configuration] = {
            "path": str(executable),
            "sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        }
        engine = executable.parent / "gneiss_engine.dll"
        if engine.is_file():
            report["executables"][configuration]["engine_sha256"] = hashlib.sha256(
                engine.read_bytes()
            ).hexdigest()
        for profile in ("daily", "full"):
            root = (getattr(args, profile + "_assets") or
                    args.cache / (profile + "-assets")).resolve()
            if not (root / "scenes/scene.scene.json").is_file():
                raise FileNotFoundError(root)
            conversion = json.loads((root / "conversion.json").read_text(encoding="utf-8"))
            if (conversion["archive_sha256"] != ARCHIVE_SHA256 or conversion["profile"] != profile
                    or conversion["nodes"] != 553 or conversion["renderers"] != 405):
                raise ValueError(f"{root}: 转换身份或规模不符")
            report["assets"][profile] = conversion
            for mode in ("thread", "cooperative"):
                for repetition in range(1, 4):
                    name = f"{profile}-{configuration}-{mode}-{repetition}"
                    prefix = args.output.resolve() / name
                    print(f"开始 {name}", flush=True)
                    with prefix.with_suffix(".log").open("wb") as log:
                        try:
                            completed = subprocess.run(
                                [str(executable), str(root), str(prefix), mode],
                                stdout=log, stderr=subprocess.STDOUT, timeout=1200,
                                check=False,
                            )
                            exit_code = completed.returncode
                        except subprocess.TimeoutExpired:
                            exit_code = "timeout"
                    if exit_code:
                        failures.append(f"{name}: exit={exit_code}")
                    else:
                        measured = json.loads(prefix.with_suffix(".json").read_text())
                        measured.update(configuration=configuration, profile=profile, name=name)
                        measured["observed_present_intervals"] = observed_frames(
                            prefix.with_suffix(".csv")
                        )
                        measured["image_sha256"] = hashlib.sha256(
                            prefix.with_suffix(".ppm").read_bytes()
                        ).hexdigest()
                        samples.append(measured)
                        if (measured["nodes"] != 553 or
                                measured["live_resources"] != args.expected_resources):
                            failures.append(f"{name}: 对象或资源数量不符")
                        if configuration == "release" and measured["activation_ms"] > 5:
                            failures.append(f"{name}: 激活超过 5 ms")
                        if measured["candidate_resident_bytes"] > 2 * 1024**3:
                            failures.append(f"{name}: 候选逻辑资源超过 2 GiB")
                        if measured.get("peak_upload_bytes", 0) > 64 * 1024**2:
                            failures.append(f"{name}: 本夹具上传峰值超过 64 MiB")
                        if configuration == "release" and (
                            measured["event_interval_p95_ms"] > 100
                            or measured["event_interval_max_ms"] > 250
                        ):
                            failures.append(f"{name}: 事件响应超过固定目标")
                        print(f"完成 {name}: {measured['load_ms']:.1f} ms", flush=True)
                    (args.output / "summary.json").write_text(
                        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
                    )
    groups = []
    for configuration in configurations:
        for profile in ("daily", "full"):
            for mode in ("thread", "cooperative"):
                group = [s for s in samples if s["configuration"] == configuration
                         and s["profile"] == profile and s["mode"] == mode]
                values = [s["load_ms"] for s in group]
                if len(values) != 3:
                    failures.append(f"{configuration}/{profile}/{mode}: 样本不足")
                    continue
                groups.append({
                    "configuration": configuration, "profile": profile, "mode": mode,
                    "count": len(values), "load_p50_ms": percentile(values, .5),
                    "load_p95_ms": percentile(values, .95), "load_max_ms": max(values),
                })
                if baseline and configuration == "release":
                    previous = [s for s in baseline["samples"]
                                if s["name"].startswith(f"{profile}-{mode}-") and s["exit"] == 0]
                    if len(previous) != 3:
                        failures.append(f"{profile}/{mode}: 基线样本不足")
                        continue
                    load_limit = percentile([s["result"]["load_ms"] for s in previous], .5) * 1.5
                    memory_limit = max(s["result"]["peak_resident_bytes"] for s in previous) * 1.1
                    if percentile(values, .5) > load_limit:
                        failures.append(f"{profile}/{mode}: 加载中位数超过基线 1.5 倍")
                    if max(s["peak_resident_bytes"] for s in group) > memory_limit:
                        failures.append(f"{profile}/{mode}: 进程峰值超过基线 1.1 倍")
                    if {s["image_sha256"] for s in group} != {s["image_sha256"] for s in previous}:
                        failures.append(f"{profile}/{mode}: 图像与基线不同")
    report["groups"] = groups
    report["image_consistency"] = {}
    for profile in ("daily", "full"):
        digests = sorted({s["image_sha256"] for s in samples if s["profile"] == profile})
        report["image_consistency"][profile] = digests
        if len(digests) != 1:
            failures.append(f"{profile}: 异步两配置/两模式图像不一致")
    report["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    report["status"] = "failed" if failures else "passed"
    (args.output / "summary.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
