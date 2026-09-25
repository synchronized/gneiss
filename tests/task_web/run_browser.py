# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""在真实 Chromium 中验证最小调度宿主，不以 Node 运行代替浏览器事件循环。"""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
from threading import Thread


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", required=True)
    parser.add_argument("--build", required=True, type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    if not (build / "task_web.html").is_file():
        parser.error("请先构建 tests/task_web")
    with ThreadingHTTPServer(
        ("127.0.0.1", 0), partial(SimpleHTTPRequestHandler, directory=str(build))
    ) as server, TemporaryDirectory(prefix="gneiss-task-web-") as profile:
        worker = Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            result = subprocess.run(
                [args.browser, "--headless", "--disable-gpu", "--no-first-run",
                 "--user-data-dir=" + profile, "--timeout=15000",
                 "--virtual-time-budget=5000", "--dump-dom",
                 f"http://127.0.0.1:{server.server_port}/task_web.html"],
                capture_output=True, timeout=45, check=True,
            )
            (build / "browser-dom.html").write_bytes(result.stdout)
            (build / "browser.log").write_bytes(result.stderr)
            if b'data-test-result="passed"' not in result.stdout:
                raise RuntimeError("浏览器未报告通过；检查 browser-dom.html 与 browser.log")
            print("GNEISS_TASK_WEB_PASS")
        finally:
            server.shutdown()
            worker.join()


if __name__ == "__main__":
    main()
