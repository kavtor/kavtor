#!/usr/bin/env python3
"""Read NVIDIA GPU/process activity without changing mixer state."""
import argparse
import json
import shutil
import statistics
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=10, help='Sampling window (1–300 seconds)')
    parser.add_argument('--output', type=Path, help='Optional JSON report path')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 300:
        parser.error('--seconds must be between 1 and 300')
    binary = shutil.which('nvidia-smi')
    if not binary:
        parser.error('nvidia-smi is required; this probe supports NVIDIA GPUs')
    def run(*options):
        return subprocess.check_output([binary, *options], text=True, timeout=args.seconds + 10)
    try:
        before = run('--query-gpu=index,name,utilization.gpu,memory.used,memory.total,power.draw', '--format=csv')
        samples = run('pmon', '-s', 'um', '-c', str(args.seconds))
        after = run('--query-gpu=index,name,utilization.gpu,memory.used,memory.total,power.draw', '--format=csv')
    except (subprocess.SubprocessError, OSError) as error:
        parser.exit(1, f'GPU probe failed: {error}\n')
    header = None
    processes = {}
    for line in samples.splitlines():
        if line.startswith('# gpu'):
            header = line[1:].split()
        if not header or line.startswith('#'):
            continue
        fields = line.split()
        if len(fields) < len(header):
            continue
        row = dict(zip(header, fields))
        if not row.get('pid', '').isdigit():
            continue
        key = (row.get('gpu'), row['pid'])
        item = processes.setdefault(key, {'gpu': key[0], 'pid': int(key[1]), 'name': row.get('command'), 'samples': []})
        item['samples'].append({field: float(row[field]) if row.get(field, '-').replace('.', '', 1).isdigit() else None
                                for field in ['sm', 'mem', 'enc', 'dec', 'fb']})
    for item in processes.values():
        item['mean'] = {}
        for field in ['sm', 'mem', 'enc', 'dec', 'fb']:
            values = [sample[field] for sample in item['samples'] if sample[field] is not None]
            item['mean'][field] = statistics.mean(values) if values else None
    report = {'seconds': args.seconds, 'gpu_before_csv': before, 'gpu_after_csv': after,
              'processes': list(processes.values()), 'raw_pmon': samples,
              'notes': ['Missing counters are null, not zero.',
                        'GPU utilization is activity, not percentage of maximum throughput.',
                        'Per-source costs require controlled comparisons; NVIDIA reports processes.']}
    result = json.dumps(report, indent=2)
    if args.output:
        args.output.write_text(result + '\n')
        print(f'Report written to {args.output}')
    else:
        print(result)


if __name__ == '__main__':
    main()
