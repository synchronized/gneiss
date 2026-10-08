# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""响应时间窗关联的口径与输入校验回归。"""

import unittest

from correlate_loop_response import correlate


ENGINE = '''# origin_ns=1000000000
kind,frame,start_ms,total_ms,gap_ms,update_ms,idle_wait_ms
sample,1,10,8,3,1,4
sample,2,20,5,0,1,4
maxima,2,0,8,0,1,4
'''
HEADER = 'kind,index,sent_ns,received_ns,latency_ms\n'


class CorrelationTests(unittest.TestCase):
    def test_gap_is_before_start(self):
        report = correlate(ENGINE, HEADER + 'key,1,1006000000,1017000000,11\n')
        sample = report['probes'][0]
        self.assertEqual(sample['latency_ms'], 11)
        self.assertEqual(sample['unretained_ms'], 3)
        self.assertEqual(sample['loops'][0]['gap_overlap_ms'], 3)
        self.assertEqual(sample['loops'][0]['inside_overlap_ms'], 5)

    def test_separate_loops_do_not_fill_unretained_interval(self):
        sample = correlate(ENGINE, HEADER + 'task,1,1012000000,1023000000,11\n')['probes'][0]
        self.assertEqual(len(sample['loops']), 2)
        self.assertEqual(sample['unretained_ms'], 5)

    def test_stage_totals_are_not_attributed_to_overlap(self):
        sample = correlate(ENGINE, HEADER + 'key,1,1024000000,1025000000,1\n')['probes'][0]
        loop = sample['loops'][0]
        self.assertEqual(loop['inside_overlap_ms'], 1)
        self.assertEqual(loop['loop_stage_totals_ms']['idle_wait_ms'], 4)

    def test_invalid_timestamps_and_identity(self):
        for row in ['key,1,999000000,1010000000,11', 'task,1,1010000000,1009000000,-1',
                    'bad,1,1010000000,1011000000,1', 'key,0,1010000000,1011000000,1']:
            with self.subTest(row=row), self.assertRaises(ValueError):
                correlate(ENGINE, HEADER + row + '\n')
        with self.assertRaises(ValueError):
            correlate(ENGINE, HEADER + 'key,1,1010000000,1011000000,1\n' * 2)

    def test_invalid_engine(self):
        probe = HEADER + 'key,1,1010000000,1011000000,1\n'
        for engine in [ENGINE.replace('# origin_ns=1000000000\n', ''),
                       ENGINE.replace('10,8,3', '10,2,3'),
                       ENGINE.replace('10,8,3', '10,nan,3')]:
            with self.subTest(engine=engine), self.assertRaises(ValueError):
                correlate(engine, probe)

    def test_rounding_overlap_is_not_counted_twice(self):
        engine = ENGINE.replace('sample,2,20,5,0', 'sample,2,14.9999,5,0')
        sample = correlate(engine, HEADER + 'key,1,1010000000,1020000000,10\n')['probes'][0]
        self.assertAlmostEqual(sample['unretained_ms'], .0001)

    def test_missing_samples_are_rejected(self):
        with self.assertRaises(ValueError):
            correlate(ENGINE, HEADER)
        with self.assertRaises(ValueError):
            correlate('# origin_ns=1000000000\nkind,frame\n',
                      HEADER + 'key,1,1010000000,1011000000,1\n')


if __name__ == '__main__':
    unittest.main()
