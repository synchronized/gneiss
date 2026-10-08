# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gneiss contributors

"""关联响应探针与引擎循环时间窗，不把阶段累计耗时当作阶段起止时间。"""

import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path


def correlate(engine_text, probe_text):
    lines = engine_text.splitlines()
    origins = [int(line.removeprefix('# origin_ns=')) for line in lines
               if line.startswith('# origin_ns=')]
    if len(origins) != 1:
        raise ValueError('需要恰好一个引擎循环时间原点')
    rows = list(csv.DictReader(io.StringIO('\n'.join(
        line for line in lines if not line.startswith('#')))))
    loops = []
    for row in rows:
        if row['kind'] != 'sample':
            continue
        values = {key: float(value) for key, value in row.items() if key.endswith('_ms')}
        if not all(math.isfinite(value) and value >= 0 for value in values.values()):
            raise ValueError('循环包含无效耗时')
        start, total, gap = (values[key] for key in ['start_ms', 'total_ms', 'gap_ms'])
        if total < gap:
            raise ValueError('循环总间隔小于循环前空档')
        # total 包含 gap：真实循环区间为 [start, start + total - gap]。
        loops.append((int(row['frame']), start, start + total - gap, gap, values))
    if not loops:
        raise ValueError('缺少保留的循环样本')
    results = []
    seen = set()
    for probe in csv.DictReader(io.StringIO(probe_text)):
        kind, index = probe['kind'], int(probe['index'])
        sent, received = int(probe['sent_ns']), int(probe['received_ns'])
        if kind not in ('key', 'task') or index <= 0 or (kind, index) in seen:
            raise ValueError('探针类型、序号无效或重复')
        seen.add((kind, index))
        if sent < origins[0] or received < sent:
            raise ValueError('探针时间窗无效')
        begin, end = (sent - origins[0]) / 1e6, (received - origins[0]) / 1e6
        matches, intervals = [], []
        for frame, start, finish, gap, values in loops:
            inside = max(0.0, min(end, finish) - max(begin, start))
            outside = max(0.0, min(end, start) - max(begin, start - gap))
            if inside > 0 or outside > 0:
                matches.append({'frame': frame, 'inside_overlap_ms': inside,
                                'gap_overlap_ms': outside, 'loop_stage_totals_ms': values})
                intervals.append((max(begin, start - gap), min(end, finish)))
        # 合并时间窗，避免 CSV 四舍五入导致邻接样本微小重叠而重复累计。
        covered, previous_end = 0.0, begin
        for start, finish in sorted(intervals):
            covered += max(0.0, finish - max(start, previous_end))
            previous_end = max(previous_end, finish)
        results.append({'kind': kind, 'index': index, 'latency_ms': end - begin,
                        'start_ms': begin, 'end_ms': end, 'loops': matches,
                        'unretained_ms': max(0.0, end - begin - covered)})
    if not results:
        raise ValueError('缺少响应探针')
    return {'status': 'diagnostic_only', 'origin_ns': origins[0],
            'limits': '仅关联保留的循环时间窗。阶段为整轮累计且可能嵌套，不能累加或认定与探针完全重合；未保留时间不等于主线程停顿。不能撤销既有验收失败。',
            'probes': sorted(results, key=lambda item: item['latency_ms'], reverse=True)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine', type=Path)
    parser.add_argument('probes', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = correlate(args.engine.read_text(encoding='utf-8'),
                       args.probes.read_text(encoding='utf-8'))
    report['inputs'] = [{'path': path.as_posix(),
                         'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                        for path in [args.engine, args.probes]]
    with args.output.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(f"已关联 {len(report['probes'])} 个保留探针；该报告不是性能验收结论。")


if __name__ == '__main__':
    main()
