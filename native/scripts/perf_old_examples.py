#!/usr/bin/env python3
"""CPU/GPU/total frame time of the original example scenes: C renderer (Citro3D) versus the native Rust renderer.

Runs build/perf_<case>_{reference,native}.3dsx (the scene statements of native/compare/old_examples.cpp repeated for
30 warmup and 120 measured frames) one emulator launch each. Azahar counts about one tick per ARM instruction, so the
CPU figures follow instruction counts; its GPU times are not modelled (see docs/native-rewrite.md).
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ALL = ['cube', 'overlay', 'primitives', 'primitives2', 'primitives3', 'texture', 'texcoords', 'texmatrix', 'multitexture',
       'mipmap', 'compressed', 'lighting', 'texgen', 'fog', 'fragment', 'fragment2', 'clipplane', 'lists', 'select',
       'stipple', 'accum', 'pixels', 'points', 'glu0', 'glu1', 'glu2']
parser = argparse.ArgumentParser()
parser.add_argument('--emulator', required=True)
parser.add_argument('--sdmc', required=True, type=Path)
parser.add_argument('--build', type=Path, default=Path('build-native-old-examples'))
parser.add_argument('--output', type=Path, default=Path('docs/native-results/perf-old-examples'))
parser.add_argument('--case', action='append', choices=ALL)
parser.add_argument('--runs', type=int, default=1)
parser.add_argument('--timeout', type=float, default=300)
parser.add_argument('--backend', action='append', choices=['reference', 'native'])
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
args.output.mkdir(parents=True, exist_ok=True)
pattern = re.compile(r'perf case=(\S+) backend=(\S+) frames=(\d+) CPU ([\d.]+) GPU ([\d.]+) total ([\d.]+) cmd ([\d.]+)')
previous = json.loads((args.output / 'summary.json').read_text()) if (args.output / 'summary.json').exists() else {}
results = {}
for case in args.case or ALL:
    entry = results.setdefault(case, {'reference': [], 'native': []})
    for backend in ['reference', 'native']:
        if args.backend and backend not in args.backend:
            entry[backend] = previous.get(case, {}).get(backend, [])
    for run in range(args.runs):
        for backend in args.backend or ['reference', 'native']:
            rom = args.build / f'perf_{case}_{backend}.3dsx'
            destination = args.output / case / f'{backend}-{run}'
            subprocess.run([sys.executable, str(root / 'native/scripts/smoke_check.py'), '--emulator', args.emulator,
                            '--rom', str(rom), '--sdmc', str(args.sdmc),
                            '--report', f'c3dgl-old-{case}-{backend}-results.txt',
                            '--output', str(destination), '--timeout', str(args.timeout)], check=False,
                           stdout=subprocess.DEVNULL)
            text = (destination / 'results.txt').read_text() if (destination / 'results.txt').exists() else ''
            match = pattern.search(text)
            if not match:
                entry[backend].append(None)
                print(f'{case} {backend}: FAILED {text.strip()[:120]!r}', flush=True)
                continue
            cpu, gpu, total, cmd = (float(match.group(i)) for i in range(4, 8))
            entry[backend].append(dict(cpu_ms=cpu, gpu_ms=gpu, total_ms=total, cmd=cmd))
            print(f'{case} {backend}: CPU {cpu:.3f} GPU {gpu:.3f} total {total:.3f}', flush=True)
    (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')


def best(runs, key):
    values = [r[key] for r in runs if r]
    return min(values) if values else None


rows = ['| scene | CPU C ms | CPU Rust ms | CPU change | GPU C ms | GPU Rust ms | total C ms | total Rust ms |',
        '|---|---:|---:|---:|---:|---:|---:|---:|']
for case, entry in results.items():
    ref, nat = entry['reference'], entry['native']
    rc, nc = best(ref, 'cpu_ms'), best(nat, 'cpu_ms')
    if rc is None or nc is None:
        rows.append(f'| {case} | {"n/a" if rc is None else f"{rc:.2f}"} | {"n/a" if nc is None else f"{nc:.2f}"} | failed | | | | |')
        continue
    rows.append(f'| {case} | {rc:.2f} | {nc:.2f} | {100 * (nc / rc - 1):+.1f}% | {best(ref, "gpu_ms"):.2f} | '
                f'{best(nat, "gpu_ms"):.2f} | {best(ref, "total_ms"):.2f} | {best(nat, "total_ms"):.2f} |')
(args.output / 'summary.md').write_text('\n'.join(rows) + '\n')
print('\n'.join(rows))
