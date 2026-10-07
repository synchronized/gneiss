# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""验证冻结门槛不会被缺样本、重复样本或局部回退绕过。"""

import copy
import unittest

from validate_runtime_loop import validate


def report(main_cpu):
    return {"status": "passed", "platform": "fixture", "cache_policy": "unchanged",
            "samples": [{"profile": profile, "trace": trace, "repeat": repeat, "exit": 0,
                         "elapsed_ms": 100, "main_thread_cpu_ms": main_cpu,
                         "process_cpu_ms": 180, "sampled_peak_working_set_bytes": 100,
                         "tables": [{"maxima": {"total_ms": "80"}}]}
                        for profile in ["daily", "full"] for trace in [False, True]
                        for repeat in [1, 2, 3]]}


class Gates(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(validate(report(90), report(20))["status"], "passed")

    def test_independent_failures(self):
        changes = [lambda r: r.update(status="running"),
                   lambda r: r.update(platform="other"),
                   lambda r: r["samples"].pop(),
                   lambda r: r["samples"][0].update(exit=1),
                   lambda r: r["samples"][0].update(repeat=2),
                   lambda r: r["samples"][0].update(sampled_peak_working_set_bytes=111),
                   lambda r: r["samples"][0].update(elapsed_ms=float("nan")),
                   lambda r: r["samples"][3].update(tables=[]),
                   lambda r: r["samples"][3]["tables"][0]["maxima"].update(total_ms="101")]
        for change in changes:
            with self.subTest(change=change):
                candidate = copy.deepcopy(report(20))
                change(candidate)
                self.assertEqual(validate(report(90), candidate)["status"], "failed")

    def test_median_gates(self):
        for field, value in [("main_thread_cpu_ms", 26), ("elapsed_ms", 111),
                             ("process_cpu_ms", 190)]:
            with self.subTest(field=field):
                candidate = report(20)
                for sample in candidate["samples"]:
                    sample[field] = value
                self.assertEqual(validate(report(90), candidate)["status"], "failed")


if __name__ == "__main__":
    unittest.main()
