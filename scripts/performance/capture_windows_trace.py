# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""在管理员 Windows 终端联合采集 WPR 与 Tracy；失败夹具也保存现场。"""

import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def command(arguments, log, timeout=120):
    with log.open("w", encoding="utf-8") as stream:
        return subprocess.run(
            arguments, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), check=False,
        ).returncode


def start_process(arguments, stream, environment):
    return subprocess.Popen(
        arguments, stdout=stream, stderr=subprocess.STDOUT, env=environment,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )


def capture(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    instance = "Gneiss-" + uuid.uuid4().hex
    stop = [str(args.wpr), "-stop", str(output / "system.etl"), "-skipPdbGen",
            "-instancename", instance]
    record = {
        "started_utc": utc_now(), "instance": instance, "stop_command": stop,
        "executable": str(args.exe), "assets": str(args.assets),
        "timeout_seconds": args.timeout, "diagnostic_only": True,
        "hashes": {},
    }
    for path in (args.exe, args.exe.parent / "gneiss_engine.dll",
                 args.exe.parent / "gneiss_tracy.dll"):
        if path.is_file():
            with path.open("rb") as stream:
                record["hashes"][path.name] = hashlib.file_digest(stream, "sha256").hexdigest()

    def save():
        (output / "run.json").write_text(
            json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    save()  # 启动前保存专属实例的恢复命令，不操作其他 WPR 会话。
    recording = False
    fixture = collector = None
    environment = os.environ.copy()
    environment.pop("GNEISS_LOOP_TRACE", None)
    environment.pop("GNEISS_RESPONSE_STACK_SAMPLE", None)
    try:
        record["wpr_start_exit"] = command(
            [str(args.wpr), "-start", "GeneralProfile", "-start", "FileIO", "-filemode",
             "-instancename", instance], output / "wpr-start.log", timeout=30)
        if record["wpr_start_exit"] != 0:
            print(f"WPR 启动失败，未启动夹具；详见 {output / 'wpr-start.log'}。")
            return 2
        recording = True
        with (output / "tracy.log").open("w") as trace_log, (output / "fixture.log").open("w") as fixture_log:
            try:
                collector = start_process(
                    [str(args.tracy), "-a", "127.0.0.1", "-o", str(output / "application.tracy"),
                     "-s", str(args.timeout + 60)], trace_log, environment)
                fixture = start_process(
                    [str(args.exe), str(output / "response.json"), "--assets", str(args.assets)],
                    fixture_log, environment)
                record["fixture_pid"] = fixture.pid
                record["fixture_started_utc"] = utc_now()
                save()
                print(f"采集进程 {fixture.pid}，输出 {output}", flush=True)
                record["fixture_exit"] = fixture.wait(timeout=args.timeout)
            finally:
                if fixture is not None and fixture.poll() is None:
                    fixture.kill()
                    fixture.wait(timeout=15)
                record["fixture_finished_utc"] = utc_now()
                if collector is not None:
                    try:
                        record["tracy_exit"] = collector.wait(timeout=30)
                    except subprocess.TimeoutExpired:
                        collector.kill()
                        collector.wait(timeout=15)
                        record["tracy_timeout"] = True
    except (OSError, subprocess.TimeoutExpired, KeyboardInterrupt) as error:
        record["error"] = f"{type(error).__name__}: {error}"
    finally:
        if recording:
            try:
                record["wpr_stop_exit"] = command(stop, output / "wpr-stop.log")
            except (OSError, subprocess.TimeoutExpired, KeyboardInterrupt) as error:
                record["wpr_stop_error"] = f"{type(error).__name__}: {error}"
            # 保存失败时保留会话，按 run.json 的 stop_command 恢复；不自动 cancel 丢证据。
        record["finished_utc"] = utc_now()
        record["artifacts"] = {
            name: (output / name).stat().st_size if (output / name).is_file() else 0
            for name in ("system.etl", "application.tracy", "response.json")
        }
        save()
    if (record.get("error") or record.get("wpr_stop_exit") != 0
            or record.get("tracy_exit") != 0 or not all(record["artifacts"].values())):
        print(f"采集未完整完成，请检查 {output / 'run.json'}；WPR 恢复命令也记录于此。")
        return 2
    # 性能失败仍返回夹具的非零状态，已保存的采集文件不删除。
    return record["fixture_exit"]


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--exe", type=Path, default=root / "build/windows-clang-profiling/bin/gneiss_main_loop_response.exe")
    parser.add_argument("--tracy", type=Path, default=root / "build/tracy-tools/unpacked/tracy-capture.exe")
    parser.add_argument("--output", type=Path, default=root / "build" / ("windows-trace-" + datetime.now().strftime("%Y%m%d-%H%M%S")))
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--check", action="store_true", help="仅验证路径、工具和管理员条件，不启动采集")
    args = parser.parse_args()
    args.exe, args.tracy, args.assets = args.exe.resolve(), args.tracy.resolve(), args.assets.resolve()
    args.wpr = shutil.which("wpr")
    checks = {
        "windows": os.name == "nt", "executable": args.exe.is_file(),
        "tracy": args.tracy.is_file(), "wpr": args.wpr is not None,
        "scene": (args.assets / "scenes/scene.scene.json").is_file(),
        "new_output": not args.output.exists(), "positive_timeout": args.timeout > 0,
        "administrator": os.name == "nt" and bool(ctypes.windll.shell32.IsUserAnAdmin()),
    }
    if args.check or not all(checks.values()):
        print(json.dumps(checks, ensure_ascii=False, indent=2))
    if not all(checks.values()):
        print("未满足采集条件；系统事件采集需要管理员终端。未启动夹具或 WPR。")
        return 2
    return 0 if args.check else capture(args)


if __name__ == "__main__":
    raise SystemExit(main())
