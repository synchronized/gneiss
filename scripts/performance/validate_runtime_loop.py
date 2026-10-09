# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""检查 DEV-050 已冻结的 Runtime CPU、吞吐、工作集与循环门槛。"""

import argparse
import json
import math
from pathlib import Path
import statistics


def validate(baseline, candidate):
    failures = []
    groups = []
    for name, report in [("baseline", baseline), ("candidate", candidate)]:
        if report.get("status") != "passed":
            failures.append(f"{name}: 报告未完整通过")
    for field in ["platform", "cache_policy"]:
        if not baseline.get(field) or baseline.get(field) != candidate.get(field):
            failures.append(f"{field}: 测量条件缺失或不同")
    for profile in ["daily", "full"]:
        for trace in [False, True]:
            previous = [s for s in baseline.get("samples", [])
                        if s.get("profile") == profile and s.get("trace") is trace]
            current = [s for s in candidate.get("samples", [])
                       if s.get("profile") == profile and s.get("trace") is trace]
            label = f"{profile}/{'trace' if trace else 'plain'}"
            if (len(previous) < 3 or len(current) < 3
                    or len({s.get("repeat") for s in previous}) < 3
                    or len({s.get("repeat") for s in current}) < 3):
                failures.append(f"{label}: 至少需要三次基线和对照")
                continue
            valid = True
            for sample in previous + current:
                if sample.get("exit") != 0:
                    failures.append(f"{label}: 存在进程失败")
                    valid = False
                for key in ["elapsed_ms", "main_thread_cpu_ms", "process_cpu_ms",
                            "sampled_peak_working_set_bytes"]:
                    value = sample.get(key)
                    if not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
                        failures.append(f"{label}: {key} 计数缺失或无效")
                        valid = False
            if not valid:
                continue
            if trace:
                for sample in current:
                    tables = sample.get("tables", [])
                    if not tables:
                        failures.append(f"{label}: 缺少循环诊断")
                    for table in tables:
                        maximum = float(table.get("maxima", {}).get("total_ms", "nan"))
                        if not math.isfinite(maximum) or maximum < 0 or maximum > 100:
                            failures.append(f"{label}: 循环最大间隔缺失或超过 100 ms")
                continue
            elapsed = statistics.median(s["elapsed_ms"] for s in current)
            elapsed_ratio = elapsed / statistics.median(s["elapsed_ms"] for s in previous)
            main_core = statistics.median(s["main_thread_cpu_ms"] / s["elapsed_ms"] for s in current)
            process_ratio = (statistics.median(s["process_cpu_ms"] for s in current)
                             / statistics.median(s["process_cpu_ms"] for s in previous))
            memory_ratio = (max(s["sampled_peak_working_set_bytes"] for s in current)
                            / max(s["sampled_peak_working_set_bytes"] for s in previous))
            groups.append({"profile": profile, "elapsed_ms": elapsed, "elapsed_ratio": elapsed_ratio,
                           "main_core": main_core, "process_cpu_ratio": process_ratio,
                           "peak_working_set_ratio": memory_ratio})
            for name, value, limit in [("主线程核心占用", main_core, .25),
                                       ("加载耗时比", elapsed_ratio, 1.10),
                                       ("进程 CPU 比", process_ratio, 1.05),
                                       ("采样工作集峰值比", memory_ratio, 1.10)]:
                if value > limit:
                    failures.append(f"{label}: {name} {value:.6f} 超过 {limit}")
    return {"status": "failed" if failures else "passed", "groups": groups,
            "failures": failures,
            "scope": "仅验证 Runtime 重复矩阵；不替代资产身份、输入响应、图像、生命周期或发布平台验收。"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = validate(json.loads(args.baseline.read_text(encoding="utf-8")),
                      json.loads(args.candidate.read_text(encoding="utf-8")))
    with args.output.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
