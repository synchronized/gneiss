# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""顺序验证完整场景生命周期；保留失败结果，不复用已有输出目录。"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


SCENARIOS = ("repeat", "cancel-prepare", "cancel-assets", "cancel-gpu", "cancel-verify",
             "cancel-ready", "failure", "interact", "close")


def validate(data, scenario, performance):
    failures = []
    if scenario == "close":
        if not data["closed"] or data["retained_tasks"]:
            failures.append("关闭后任务未回收")
        return failures
    if data["nodes"] != 553 or data["live_resources"] != 505:
        failures.append("节点或资源数量变化")
    if scenario == "repeat" and data["completed_switches"] != 3:
        failures.append("连续切换不足三次")
    if scenario.startswith("cancel-"):
        if data["cancel_ms"] <= 0:
            failures.append("未观察到取消终态")
        if performance and data["cancel_ms"] > 250:
            failures.append("取消终态超过250ms")
        # 必须包括等待上传与回滚的时间，不能使用局部析构计时冒充总清理等待。
        if performance and data["cancel_cleanup_ms"] > 1000:
            failures.append("取消后的实际清理超过1000ms")
    if scenario.startswith("cancel-") or scenario == "failure":
        if not data["cleanup_complete"] or data["upload_reserved_bytes"]:
            failures.append("清理完成或上传预留检查失败")
    if scenario == "failure" and not data["retried"]:
        failures.append("未完成失败重试")
    if scenario == "interact" and (data["resizes"] != 10 or not data["minimized"]
                                   or not data["restored"]):
        failures.append("窗口交互不完整")
    if performance and (data["event_interval_p95_ms"] > 100
                        or data["event_interval_max_ms"] > 250):
        failures.append("主循环响应超过冻结门槛")
    if data["candidate_resident_bytes"] > 2 * 1024**3 or data["peak_upload_bytes"] > 64 * 1024**2:
        failures.append("候选或上传预算超过冻结门槛")
    if performance and max(data["activation_ms"], data["maximum_activation_ms"]) > 5:
        failures.append("激活超过5ms")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("assets", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--modes", nargs="+", choices=("thread", "cooperative"),
                        default=("thread", "cooperative"))
    parser.add_argument("--scenarios", nargs="+", choices=SCENARIOS, default=SCENARIOS)
    parser.add_argument("--functional-only", action="store_true",
                        help="用于 Debug 功能检查，不判断 Release 性能门槛")
    parser.add_argument("--image-sha256", help="可选的同输入、同视角基线图像摘要")
    args = parser.parse_args()
    executable = args.executable.resolve()
    assets = args.assets.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {
        "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "executable": str(executable),
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "assets": str(assets), "functional_only": args.functional_only,
        "status": "running", "samples": [], "failures": [],
    }
    engine = executable.parent / "gneiss_engine.dll"
    if engine.is_file():
        report["engine_sha256"] = hashlib.sha256(engine.read_bytes()).hexdigest()

    def save():
        (output / "summary.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    for mode in args.modes:
        for scenario in args.scenarios:
            name = f"{mode}-{scenario}"
            prefix = output / name
            print("开始", name, flush=True)
            with prefix.with_suffix(".log").open("wb") as log:
                try:
                    status = subprocess.run(
                        [str(executable), str(assets), str(prefix), mode, scenario],
                        stdout=log, stderr=subprocess.STDOUT, timeout=2400).returncode
                except subprocess.TimeoutExpired:
                    status = "timeout"
            sample = {"name": name, "exit": status}
            failures = []
            if status:
                failures.append(f"进程失败：{status}")
            else:
                try:
                    data = json.loads(prefix.with_suffix(".json").read_text(encoding="utf-8"))
                    sample["result"] = data
                    failures.extend(validate(data, scenario, not args.functional_only))
                    if scenario != "close":
                        digest = hashlib.sha256(prefix.with_suffix(".ppm").read_bytes()).hexdigest()
                        sample["image_sha256"] = digest
                        if args.image_sha256 and digest != args.image_sha256:
                            failures.append("图像与基线不一致")
                except (OSError, ValueError, KeyError, TypeError) as error:
                    failures.append(f"结果读取失败：{error}")
            report["samples"].append(sample)
            report["failures"].extend(f"{name}: {failure}" for failure in failures)
            if failures:
                report["status"] = "failed"
                save()
                print(report["failures"], flush=True)
                return 1
            save()
            print("完成", name, flush=True)
    report["status"] = "passed"
    save()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
