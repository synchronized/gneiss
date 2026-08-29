#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""重复运行稳定样例并保存可复查的性能与内存数据。"""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
import platform
import statistics
import subprocess
import threading
import time
from pathlib import Path
from typing import Any


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path, help="稳定运行时样例可执行文件")
    parser.add_argument("--output", type=Path, required=True, help="输出 JSON 文件")
    parser.add_argument("--samples", type=int, default=10, help="有效样本数，默认 10")
    parser.add_argument("--warmup-runs", type=int, default=1, help="不计入结果的预热进程数")
    parser.add_argument("--compiler", required=True, help="编译器及版本")
    parser.add_argument("--build-type", required=True, help="构建类型，例如 Release")
    parser.add_argument("--cpu", required=True, help="CPU 型号")
    parser.add_argument("--gpu", required=True, help="实际使用的 GPU 型号")
    parser.add_argument("--gpu-driver", required=True, help="GPU 驱动版本")
    parser.add_argument("--gneiss-revision", required=True, help="Gneiss 提交")
    parser.add_argument("--granit-revision", required=True, help="Granit 提交")
    return parser.parse_args()


def linux_rss_bytes(process_id: int) -> int | None:
    try:
        for line in Path(f"/proc/{process_id}/status").read_text(encoding="utf-8").splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) * 1024
    except (FileNotFoundError, PermissionError, ProcessLookupError, ValueError):
        return None
    return None


def windows_rss_bytes(process_id: int) -> int | None:
    class ProcessMemoryCounters(ctypes.Structure):
        _fields_ = [
            ("cb", ctypes.c_ulong),
            ("page_fault_count", ctypes.c_ulong),
            ("peak_working_set_size", ctypes.c_size_t),
            ("working_set_size", ctypes.c_size_t),
            ("quota_peak_paged_pool_usage", ctypes.c_size_t),
            ("quota_paged_pool_usage", ctypes.c_size_t),
            ("quota_peak_non_paged_pool_usage", ctypes.c_size_t),
            ("quota_non_paged_pool_usage", ctypes.c_size_t),
            ("pagefile_usage", ctypes.c_size_t),
            ("peak_pagefile_usage", ctypes.c_size_t),
        ]

    process_query_information = 0x0400
    process_vm_read = 0x0010
    kernel32 = ctypes.windll.kernel32
    psapi = ctypes.windll.psapi
    kernel32.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel32.CloseHandle.restype = ctypes.c_int
    psapi.GetProcessMemoryInfo.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ProcessMemoryCounters),
        ctypes.c_ulong,
    ]
    psapi.GetProcessMemoryInfo.restype = ctypes.c_int
    handle = kernel32.OpenProcess(process_query_information | process_vm_read, False, process_id)
    if not handle:
        return None
    try:
        counters = ProcessMemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
            return None
        return int(counters.working_set_size)
    finally:
        kernel32.CloseHandle(handle)


def current_rss_bytes(process_id: int) -> int | None:
    if os.name == "nt":
        return windows_rss_bytes(process_id)
    if platform.system() == "Linux":
        return linux_rss_bytes(process_id)
    return None


def run_sample(executable: Path) -> dict[str, Any]:
    started = time.perf_counter()
    process = subprocess.Popen(
        [str(executable), "--measure"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
    )
    peak_rss = 0
    monitoring = True

    def monitor() -> None:
        nonlocal peak_rss
        while monitoring:
            rss = current_rss_bytes(process.pid)
            if rss is not None:
                peak_rss = max(peak_rss, rss)
            time.sleep(0.01)

    monitor_thread = threading.Thread(target=monitor, daemon=True)
    monitor_thread.start()
    stdout, stderr = process.communicate()
    monitoring = False
    monitor_thread.join()
    wall_ms = (time.perf_counter() - started) * 1000.0
    if process.returncode != 0:
        raise RuntimeError(
            f"样例退出码为 {process.returncode}：{stderr.strip() or stdout.strip()}"
        )
    lines = [line for line in stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise RuntimeError(f"样例必须输出一行 JSON，实际为 {len(lines)} 行")
    result = json.loads(lines[0])
    if result.get("schema") != 1:
        raise RuntimeError("不支持的样例测量 Schema")
    result["process_wall_ms"] = wall_ms
    result["peak_rss_bytes"] = peak_rss if peak_rss > 0 else None
    return result


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    index = max(0, math.ceil(fraction * len(ordered)) - 1)
    return ordered[index]


def summarize(samples: list[dict[str, Any]]) -> dict[str, dict[str, float]]:
    keys = sorted(
        key
        for key, value in samples[0].items()
        if key not in {"schema", "warmup_frames", "sample_frames"}
        and isinstance(value, (int, float))
        and not isinstance(value, bool)
    )
    summary: dict[str, dict[str, float]] = {}
    for key in keys:
        values = [float(sample[key]) for sample in samples if sample.get(key) is not None]
        if not values:
            continue
        summary[key] = {
            "min": min(values),
            "median": statistics.median(values),
            "p95": percentile(values, 0.95),
            "max": max(values),
        }
    return summary


def main() -> int:
    arguments = parse_arguments()
    executable = arguments.executable.resolve()
    if arguments.samples < 1 or arguments.warmup_runs < 0:
        raise ValueError("样本数必须为正数，预热次数不能为负数")
    if not executable.is_file():
        raise FileNotFoundError(f"找不到样例可执行文件：{executable}")

    for _ in range(arguments.warmup_runs):
        run_sample(executable)
    samples = [run_sample(executable) for _ in range(arguments.samples)]
    report = {
        "schema": 1,
        "environment": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
            "compiler": arguments.compiler,
            "build_type": arguments.build_type,
            "cpu": arguments.cpu,
            "gpu": arguments.gpu,
            "gpu_driver": arguments.gpu_driver,
            "gneiss_revision": arguments.gneiss_revision,
            "granit_revision": arguments.granit_revision,
            "executable": str(executable),
        },
        "warmup_runs": arguments.warmup_runs,
        "sample_count": arguments.samples,
        "samples": samples,
        "summary": summarize(samples),
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"已写出 {arguments.samples} 个样本：{arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
