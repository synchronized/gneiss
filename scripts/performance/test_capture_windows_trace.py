# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""采集编排的失败/超时清理测试；不启动真实系统会话。"""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

import capture_windows_trace as capture


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.args = argparse.Namespace(
            output=root / "capture", exe=root / "fixture.exe", tracy=root / "tracy.exe",
            assets=root / "assets", wpr="wpr", timeout=1)
        self.args.exe.write_bytes(b"fixture")

    def record(self):
        return json.loads((self.args.output / "run.json").read_text(encoding="utf-8"))

    def process(self, code):
        process = Mock(pid=123)
        process.wait.return_value = code
        process.poll.return_value = code
        return process

    def successful_command(self, arguments, log, timeout=120):
        if "-stop" in arguments:
            (self.args.output / "system.etl").write_bytes(b"etl")
        return 0

    def start_pair(self, collector, fixture):
        (self.args.output / "application.tracy").write_bytes(b"trace")
        (self.args.output / "response.json").write_text('{"passed":false}')
        return [collector, fixture]

    def test_start_failure_never_stops_another_session(self):
        with patch.object(capture, "command", return_value=5) as command, patch.object(capture, "start_process") as start:
            self.assertEqual(capture.capture(self.args), 2)
        self.assertEqual(command.call_count, 1)
        start.assert_not_called()
        self.assertNotIn("wpr_stop_exit", self.record())

    def test_existing_output_is_not_overwritten(self):
        self.args.output.mkdir()
        sentinel = self.args.output / "run.json"
        sentinel.write_text("existing")
        with patch.object(capture, "command") as command:
            with self.assertRaises(FileExistsError):
                capture.capture(self.args)
        command.assert_not_called()
        self.assertEqual(sentinel.read_text(), "existing")

    def test_process_start_failure_still_saves_owned_wpr(self):
        with patch.object(capture, "command", side_effect=self.successful_command) as command, patch.object(capture, "start_process", side_effect=OSError("start failed")):
            self.assertEqual(capture.capture(self.args), 2)
        self.assertEqual(command.call_count, 2)
        self.assertIn("OSError", self.record()["error"])

    def test_fixture_failure_preserves_both_captures_and_instance(self):
        collector, fixture = self.process(0), self.process(3)

        def start(arguments, stream, environment):
            self.assertNotIn("GNEISS_LOOP_TRACE", environment)
            self.start_pair(collector, fixture)
            return collector if "-a" in arguments else fixture

        with patch.object(capture, "command", side_effect=self.successful_command) as command, patch.object(capture, "start_process", side_effect=start):
            self.assertEqual(capture.capture(self.args), 3)
        calls = [call.args[0] for call in command.call_args_list]
        self.assertEqual(calls[0][-2:], calls[1][-2:])
        self.assertEqual(calls[0][-2], "-instancename")
        self.assertTrue(all(self.record()["artifacts"].values()))
        self.assertEqual(self.record()["fixture_exit"], 3)

    def test_fixture_timeout_kills_owned_process_and_saves_wpr(self):
        collector, fixture = self.process(0), self.process(None)
        fixture.wait.side_effect = [subprocess.TimeoutExpired("fixture", 1), 0]
        with patch.object(capture, "command", side_effect=self.successful_command) as command, patch.object(capture, "start_process", side_effect=[collector, fixture]):
            self.assertEqual(capture.capture(self.args), 2)
        fixture.kill.assert_called_once()
        self.assertEqual(command.call_count, 2)
        self.assertIn("TimeoutExpired", self.record()["error"])
        self.assertEqual(self.record()["wpr_stop_exit"], 0)

    def test_collector_timeout_is_not_success(self):
        collector, fixture = self.process(None), self.process(0)
        collector.wait.side_effect = [subprocess.TimeoutExpired("collector", 30), 0]
        with patch.object(capture, "command", side_effect=self.successful_command), patch.object(capture, "start_process", side_effect=[collector, fixture]):
            self.assertEqual(capture.capture(self.args), 2)
        collector.kill.assert_called_once()
        self.assertTrue(self.record()["tracy_timeout"])

    def test_stop_failure_retains_recovery_command_without_cancel(self):
        with patch.object(capture, "command", side_effect=[0, 5]) as command, patch.object(capture, "start_process", side_effect=[self.process(0), self.process(0)]):
            self.assertEqual(capture.capture(self.args), 2)
        self.assertEqual(self.record()["wpr_stop_exit"], 5)
        self.assertEqual(self.record()["stop_command"][-1], self.record()["instance"])
        self.assertFalse(any("-cancel" in call.args[0] for call in command.call_args_list))


if __name__ == "__main__":
    unittest.main()
