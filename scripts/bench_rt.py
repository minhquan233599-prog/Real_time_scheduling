#!/usr/bin/env python3
"""Tổng hợp thời gian đáp ứng từ trace; không thay checker thứ tự EDF/RM."""
import argparse
import csv
from pathlib import Path
import re
import statistics
import sys

FIELDS = 'seq type cpu pid job tick release deadline next_release T C remaining kr aux misses'.split()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def response_samples(part, policy, taskset, sample):
    # Mỗi đoạn SOURCE là một lượt workload, không phải một job.
    head = re.search(r'SOURCE=XV6 policy=(edf|rm) set=(safe|compare) jobs=(\d+)[^\n]*input_hash=([0-9a-fA-F]+)', part)
    require(head is not None, 'thiếu SOURCE/input_hash')
    require((head[1], head[2], int(head[3])) == (policy, taskset, 3), 'sai policy/set/NJOB')
    inputs = re.findall(r'^INPUT ([ABC]): C=(\d+) T=(\d+) blocks=(\d+)$', part, re.M)
    mapping = re.findall(r'^TASK ([ABC]): pid=(\d+)$', part, re.M)
    require(len(inputs) == len(mapping) == 3, 'thiếu/trùng INPUT hoặc TASK')
    inputs = {name: tuple(map(int, values)) for name, *values in inputs}
    pids = {name: int(pid) for name, pid in mapping}
    require(set(inputs) == set(pids) == set('ABC') and len(set(pids.values())) == 3, 'sai ánh xạ task/PID')
    for marker in ('CHECK workers: PASS', 'CHECK trace export: PASS',
                   'CHECK no deadline miss: PASS', 'WORKLOAD RESULT: PASS'):
        require(part.splitlines().count(marker) == 1, 'thiếu/trùng ' + marker)
    rows = []
    for line in part.splitlines():
        if not line.startswith('CSV,') or line.startswith('CSV,seq,'):
            continue
        values = line.split(',')[1:]
        require(len(values) == len(FIELDS), 'CSV thiếu cột hoặc log bị cắt')
        rows.append(dict(zip(FIELDS, map(int, values))))
    counts = re.findall(r'^TRACE: events=(\d+) lost=(\d+)$', part, re.M)
    require(len(counts) == 1, 'thiếu/trùng TRACE summary')
    require(int(counts[0][0]) == len(rows) > 0 and int(counts[0][1]) == 0, 'trace thiếu hoặc mất sự kiện')
    require(all(e['seq'] == i for i, e in enumerate(rows)), 'seq không liên tục')
    require(all(a['tick'] <= b['tick'] for a, b in zip(rows, rows[1:])), 'tick đi lùi')
    require(all(1 <= e['type'] <= 8 for e in rows), 'loại sự kiện không hợp lệ')
    known = set(pids.values())
    require(all(e['type'] == 8 or (e['pid'] in known and e['cpu'] == 0) for e in rows), 'RT sai PID/CPU')
    require(not any(e['type'] == 6 for e in rows), 'có deadline miss thực tế')
    samples = []
    for name in 'ABC':
        c, t, blocks = inputs[name]
        require(0 < c <= t and blocks > 0, 'đầu vào không hợp lệ')
        releases, completes = {}, {}
        for e in rows:
            if e['pid'] != pids[name] or e['type'] == 8:
                continue
            require((e['C'], e['T']) == (c, t), 'metadata sai INPUT')
            require(0 <= e['remaining'] <= c and e['deadline'] == e['release'] + t, 'metadata budget/deadline sai')
            if e['type'] == 1:
                require(e['job'] not in releases and e['tick'] == e['release'], 'RELEASE trùng hoặc sai mốc')
                releases[e['job']] = e['release']
            elif e['type'] == 5:
                require(e['job'] in releases and e['job'] not in completes, 'COMPLETE thiếu release hoặc trùng')
                require(e['release'] == releases[e['job']], 'COMPLETE đổi release')
                require(e['release'] <= e['tick'] <= e['deadline'], 'COMPLETE ngoài deadline')
                completes[e['job']] = e['tick']
        require(set(completes) == {1, 2, 3}, 'không đủ ba job hoàn thành')
        require(set(releases) in ({1, 2, 3}, {1, 2, 3, 4}), 'số release không hợp lệ')
        response = [completes[j] - releases[j] for j in (1, 2, 3)]
        samples.append(dict(taskset=taskset, policy=policy, task=name, sample=sample,
                            job1=response[0], job2=response[1], job3=response[2],
                            sample_mean=statistics.mean(response)))
    return (head[4].lower(), inputs), samples


def analyze(path, cpus):
    text = Path(path).read_text(encoding='utf-8', errors='replace')
    text = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', text).replace('\r', '')
    require(not re.search(r'\bFAIL\b|\bpanic\b', text), 'log có FAIL/panic; không loại lượt lỗi khỏi benchmark')
    observed = set(map(int, re.findall(r'-smp\s+(\d+)', text)))
    require(observed == {cpus}, 'log phải chứa lệnh QEMU -smp khớp --cpus')
    declared = re.findall(r'^BENCHMARK SET: set=(safe|compare) repetitions=(\d+)$', text, re.M)
    require(len(declared) == 2 and {s for s, _ in declared} == {'safe', 'compare'}, 'cần đúng một đợt SAFE và một đợt COMPARE')
    repetitions = {s: int(n) for s, n in declared}
    require(all(n >= 5 for n in repetitions.values()), 'cần ít nhất năm lượt mỗi bộ')
    for taskset, n in repetitions.items():
        marker = f'BENCHMARK RESULT: set={taskset} passed={n} runs={n} PASS'
        require(text.count(marker) == 1, 'đợt benchmark chưa hoàn tất: ' + taskset)
    starts = list(re.finditer(r'^BENCH RUN: set=(safe|compare) index=(\d+) runs=(\d+)$', text, re.M))
    require(len(starts) == sum(repetitions.values()), 'số BENCH RUN không khớp')
    seen = {'safe': 0, 'compare': 0}
    fixed, samples = {}, []
    for i, m in enumerate(starts):
        taskset, index, n = m[1], int(m[2]), int(m[3])
        seen[taskset] += 1
        require(index == seen[taskset] and n == repetitions[taskset], 'lượt thiếu, trùng hoặc sai thứ tự')
        part = text[m.end():starts[i + 1].start() if i + 1 < len(starts) else len(text)]
        source = list(re.finditer(r'SOURCE=XV6 ', part))
        require(len(source) == 2, 'mỗi lượt phải có cả EDF và RM')
        require(part.count('CHECK restore policy: PASS') == 1 and part.count('PAIRED WORKLOAD RESULT: PASS') == 1, 'thiếu khôi phục policy/kết quả cặp')
        for j, policy in enumerate(('edf', 'rm')):
            chunk = part[source[j].start():source[j + 1].start() if j == 0 else len(part)]
            signature, measured = response_samples(chunk, policy, taskset, index)
            if taskset not in fixed:
                fixed[taskset] = signature
            require(signature == fixed[taskset], 'workload/hash thay đổi giữa các mẫu: ' + taskset)
            samples.extend(dict(cpus=cpus, **row) for row in measured)
    return samples, fixed


def write_csv(path, rows):
    with path.open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log')
    parser.add_argument('--cpus', type=int, choices=(1, 4), required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    try:
        samples, fixed = analyze(args.log, args.cpus)
        summary = []
        for taskset in ('safe', 'compare'):
            for policy in ('edf', 'rm'):
                for task in 'ABC':
                    values = [r['sample_mean'] for r in samples if (r['taskset'], r['policy'], r['task']) == (taskset, policy, task)]
                    summary.append(dict(cpus=args.cpus, taskset=taskset, policy=policy, task=task,
                                        n=len(values), mean=statistics.mean(values), stdev=statistics.stdev(values),
                                        minimum=min(values), maximum=max(values), spread=max(values)-min(values)))
        folder = Path(args.out)
        folder.mkdir(parents=True, exist_ok=True)
        write_csv(folder / 'samples.csv', samples)
        write_csv(folder / 'summary.csv', summary)
        report = [f'# Benchmark thời gian đáp ứng — CPUS={args.cpus}', '',
                  'Đơn vị: kernel tick. Mỗi mẫu là trung bình thời gian RELEASE→COMPLETE của ba job cùng tác vụ trong một lượt.',
                  'mean/stdev/min/max/range bên dưới được tính trên các mẫu đó; stdev là độ lệch chuẩn mẫu.', '',
                  '| Bộ | Policy | Task | Số mẫu | Mean | Stdev | Min | Max | Range |',
                  '|---|---|---|---|---|---|---|---|---|']
        for r in summary:
            report.append(f"| {r['taskset']} | {r['policy'].upper()} | {r['task']} | {r['n']} | {r['mean']:.3f} | {r['stdev']:.3f} | {r['minimum']:.3f} | {r['maximum']:.3f} | {r['spread']:.3f} |")
        report.extend(['', '## Đầu vào cố định trong từng bộ', ''])
        for taskset, (digest, inputs) in fixed.items():
            report.append(f'- {taskset}: input_hash={digest}; C/T/blocks={inputs}.')
        report.extend(['', '## Phạm vi', '',
                       '- Dữ liệu đã đủ mẫu, không mất trace, không có MISS và đầu vào giữ nguyên trong từng bộ.',
                       '- Thời gian đáp ứng gồm chờ CPU và thực thi; không phải thời gian CPU chính xác, throughput hay WCET.',
                       '- Job đầu tính từ lúc đăng ký/release nên gồm cả thời gian chờ cổng worker.',
                       '- Hai cấu hình CPU có thể hiệu chuẩn ra số block khác nhau: không dùng bảng này để khẳng định tốc độ CPUS=1 so với CPUS=4 nếu đầu vào khác.',
                       '- BENCHMARK DATA: PASS không thay kết quả checker vòng đời và thứ tự EDF/RM.'])
        (folder / 'benchmark.md').write_text('\n'.join(report) + '\n', encoding='utf-8')
        print('\n'.join(report))
        print(f'\nBENCHMARK DATA: PASS — {folder}')
        return 0
    except (OSError, ValueError, KeyError, csv.Error) as exc:
        print(f'BENCHMARK DATA: FAIL ({exc})')
        return 1


if __name__ == '__main__':
    sys.exit(main())