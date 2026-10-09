# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""Windows Runtime 首载开关对照；顺序测量主线程/进程 CPU 与可选循环诊断。"""

import argparse
import csv
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def parse_trace(path):
    """每次 run 单独成表，保留的最长样本不能用于推算 P95。"""
    tables = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("kind,"):
            header = next(csv.reader([line]))
            tables.append({"samples": []})
        elif line.startswith(("sample,", "maxima,")):
            row = dict(zip(header, next(csv.reader([line]))))
            if row["kind"] == "maxima":
                tables[-1]["maxima"] = row
            else:
                tables[-1]["samples"].append(row)
        elif line.startswith("# total_ms="):
            tables[-1]["totals"] = line[2:]
    if not tables or any("maxima" not in table for table in tables):
        raise ValueError("缺少完整循环诊断")
    for table in tables:
        if len(table["samples"]) > 128:
            raise ValueError("诊断样本超出容量")
        table["retained_sample_count"] = len(table["samples"])
        table["samples"].sort(key=lambda row: float(row["total_ms"]), reverse=True)
        table["samples"] = table["samples"][:10]
    return tables


class ThreadEntry(ctypes.Structure):
    _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD),
                ("tid", wintypes.DWORD), ("pid", wintypes.DWORD),
                ("priority", wintypes.LONG), ("delta", wintypes.LONG),
                ("flags", wintypes.DWORD)]


class MemoryCounters(ctypes.Structure):
    _fields_ = [("size", wintypes.DWORD), ("faults", wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in ["peak_working_set", "working_set",
            "peak_paged", "paged", "peak_nonpaged", "nonpaged", "pagefile", "peak_pagefile"]]


class ProcessCounters:
    """只查询本工具启动的进程；退出后保留句柄以读取最终 CPU 时间。"""
    def __init__(self, pid):
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.psapi = ctypes.WinDLL("psapi", use_last_error=True)
        self.process = None
        self.thread = None
        self.tid = None
        self.peak = 0
        for name in ["OpenProcess", "OpenThread"]:
            fn = getattr(self.kernel, name)
            fn.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            fn.restype = wintypes.HANDLE
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        for name in ["GetProcessTimes", "GetThreadTimes"]:
            fn = getattr(self.kernel, name)
            fn.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
            fn.restype = wintypes.BOOL
        self.kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
        self.kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
        for name in ["Thread32First", "Thread32Next"]:
            fn = getattr(self.kernel, name)
            fn.argtypes = [wintypes.HANDLE, ctypes.POINTER(ThreadEntry)]
            fn.restype = wintypes.BOOL
        self.psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE,
            ctypes.POINTER(MemoryCounters), wintypes.DWORD]
        self.psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
        try:
            self.process = self.kernel.OpenProcess(0x0410, False, pid)
            if not self.process:
                raise ctypes.WinError(ctypes.get_last_error())
            self._primary_thread(pid)
        except BaseException:
            self.close()
            raise

    def times(self, handle, thread=False):
        values = [wintypes.FILETIME() for _ in range(4)]
        fn = self.kernel.GetThreadTimes if thread else self.kernel.GetProcessTimes
        if not fn(handle, *(ctypes.byref(value) for value in values)):
            raise ctypes.WinError(ctypes.get_last_error())
        return [(value.dwHighDateTime << 32) | value.dwLowDateTime for value in values]

    def _primary_thread(self, pid):
        snapshot = self.kernel.CreateToolhelp32Snapshot(4, 0)
        if snapshot == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        first_created = None
        try:
            entry = ThreadEntry(size=ctypes.sizeof(ThreadEntry))
            more = self.kernel.Thread32First(snapshot, ctypes.byref(entry))
            while more:
                if entry.pid == pid:
                    handle = self.kernel.OpenThread(0x0800, False, entry.tid)
                    if handle:
                        try:
                            created = self.times(handle, thread=True)[0]
                            if first_created is None or created < first_created:
                                if self.thread:
                                    self.kernel.CloseHandle(self.thread)
                                self.thread, self.tid = handle, entry.tid
                                first_created = created
                                handle = None
                        finally:
                            if handle:
                                self.kernel.CloseHandle(handle)
                more = self.kernel.Thread32Next(snapshot, ctypes.byref(entry))
        finally:
            self.kernel.CloseHandle(snapshot)
        if not self.thread:
            raise RuntimeError("未取得最早创建的主线程句柄")

    def sample_memory(self):
        counters = MemoryCounters(size=ctypes.sizeof(MemoryCounters))
        if self.psapi.GetProcessMemoryInfo(self.process, ctypes.byref(counters), counters.size):
            self.peak = max(self.peak, counters.peak_working_set)

    def result(self):
        process = self.times(self.process)
        thread = self.times(self.thread, thread=True)
        return {"process_cpu_ms": (process[2] + process[3]) / 10000,
                "main_thread_cpu_ms": (thread[2] + thread[3]) / 10000,
                "main_thread_id": self.tid,
                "main_thread_selection": "最早创建的线程，保留查询句柄直到进程退出",
                "sampled_peak_working_set_bytes": self.peak}

    def close(self):
        for handle in [self.thread, self.process]:
            if handle:
                self.kernel.CloseHandle(handle)
        self.thread = self.process = None


def run_sample(executable, project, prefix, trace, timeout):
    env = dict(os.environ)
    env.pop("GNEISS_LOOP_TRACE", None)
    if trace:
        env["GNEISS_LOOP_TRACE"] = str(prefix) + ".engine.csv"
    command = [str(executable), "--smoke", "--project", str(project),
               "--log-file", str(prefix) + ".runtime.log"]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    counters = None
    started = time.perf_counter()
    with Path(str(prefix) + ".log").open("w", encoding="utf-8") as output:
        child = subprocess.Popen(command, env=env, stdout=output,
                                 stderr=subprocess.STDOUT, startupinfo=startup)
        try:
            counters = ProcessCounters(child.pid)
            while child.poll() is None:
                counters.sample_memory()
                if time.perf_counter() - started > timeout:
                    raise TimeoutError("Runtime 首载超过单次期限")
                time.sleep(0.25)
            elapsed = (time.perf_counter() - started) * 1000
            data = counters.result()
            data.update(command=command, pid=child.pid, exit=child.returncode,
                        trace=trace, elapsed_ms=elapsed)
            data["main_thread_core_equivalent"] = data["main_thread_cpu_ms"] / elapsed
            data["process_core_equivalent"] = data["process_cpu_ms"] / elapsed
            if child.returncode:
                raise RuntimeError(f"Runtime 退出码 {child.returncode}")
            text = Path(str(prefix) + ".runtime.log").read_text(encoding="utf-8")
            if "场景已激活" not in text or "Runtime 已正常退出" not in text:
                raise RuntimeError("缺少场景激活或正常退出证据")
            if trace:
                data["tables"] = parse_trace(Path(str(prefix) + ".engine.csv"))
            return data
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
            if counters:
                counters.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--daily-project", required=True, type=Path)
    parser.add_argument("--full-project", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=900)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("当前 CPU 计数实现仅支持 Windows")
    if args.repeat < 1 or args.timeout <= 0:
        parser.error("重复次数和单次期限必须为正数")
    executable = args.runtime.resolve(strict=True)
    projects = {name: project.resolve(strict=True) for name, project in
                [("daily", args.daily_project), ("full", args.full_project)]}
    for project in projects.values():
        if not (project / "gneiss.project.json").is_file():
            parser.error(f"缺少工程描述：{project}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    summary = {"status": "running", "platform": platform.platform(),
               "cache_policy": "不清理系统缓存；每次独立进程，开关交替，禁止并行性能任务",
               "cpu_scope": "整个进程寿命，包含启动与退出；CPU 时间不等于墙钟或全部核心利用率",
               "memory_scope": "每 250 ms 查询进程历史峰值，可能漏掉最后一次采样后出现的峰值",
               "runtime_sha256": digest(executable),
               "engine_sha256": digest(executable.with_name("gneiss_engine.dll")),
               "vulkan_layers": os.environ.get("VK_INSTANCE_LAYERS"), "samples": []}
    def save():
        (output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2)
                                            + "\n", encoding="utf-8", newline="\n")
    save()
    try:
        for name, project in projects.items():
            for repeat in range(args.repeat):
                for trace in ([False, True] if repeat % 2 == 0 else [True, False]):
                    label = f"{name}-{repeat + 1}-{'trace' if trace else 'plain'}"
                    data = run_sample(executable, project, output / label, trace, args.timeout)
                    data.update(profile=name, repeat=repeat + 1)
                    summary["samples"].append(data)
                    save()
                    print(label, round(data["elapsed_ms"], 2),
                          round(data["main_thread_cpu_ms"], 2), flush=True)
        summary["groups"] = []
        for name in projects:
            for trace in [False, True]:
                group = [s for s in summary["samples"] if s["profile"] == name and s["trace"] == trace]
                summary["groups"].append({"profile": name, "trace": trace, "count": len(group),
                    "median_elapsed_ms": statistics.median(s["elapsed_ms"] for s in group),
                    "median_main_cpu_ms": statistics.median(s["main_thread_cpu_ms"] for s in group),
                    "median_process_cpu_ms": statistics.median(s["process_cpu_ms"] for s in group)})
        summary["status"] = "passed"
    except BaseException as error:
        summary["status"] = "failed"
        summary["error"] = repr(error)
        save()
        raise
    save()


if __name__ == "__main__":
    main()
