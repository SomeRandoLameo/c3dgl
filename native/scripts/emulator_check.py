#!/usr/bin/env python3
"""Run built acceptance programs sequentially and retain measured evidence.

No synthetic render or screenshot: PPMs come from emulated GPU readback.
Use --sdmc with the emulator's actual SDMC directory. The runner only terminates
processes it starts, after a result marker or a timeout.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--emulator', required=True)
parser.add_argument('--sdmc', required=True, type=Path)
parser.add_argument('--native-build', default='build-native', type=Path)
parser.add_argument('--reference-build', default='build-native-reference', type=Path)
parser.add_argument('--output', default='docs/native-results', type=Path)
parser.add_argument('--runs', default=3, type=int)
parser.add_argument('--timeout', default=180, type=float)
args = parser.parse_args()
if args.runs < 1:
    parser.error('--runs must be positive')
args.output.mkdir(parents=True, exist_ok=True)

results = []
programs = [
    ('native', args.native_build / 'c3dgl_native_triangle.3dsx', 'ALL CHECKS PASSED', 'c3dgl-native'),
    ('reference', args.reference_build / 'c3dgl_reference_triangle.3dsx', 'GL error=0', 'c3dgl-reference'),
    ('native-cpp', args.native_build / 'c3dgl_native_triangle_cpp.3dsx', 'ALL CHECKS PASSED', 'c3dgl-native-cpp'),
]
for run in range(1, args.runs + 1):
    for name, rom, marker, stem in programs:
        if name == 'native-cpp' and run != 1:
            continue
        result_file = args.sdmc / (stem + '-results.txt')
        image_file = args.sdmc / (stem + '.ppm')
        old_time = result_file.stat().st_mtime_ns if result_file.exists() else 0
        print(f'Run {run}: {name}', flush=True)
        console = args.output / f'azahar-{name}-run{run}.console.log'
        with console.open('w') as log:
            process = subprocess.Popen([args.emulator, str(rom.resolve())], stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    if result_file.exists() and result_file.stat().st_mtime_ns != old_time:
                        report = result_file.read_text()
                        if 'FAIL' in report or 'GL error=' in report and marker not in report:
                            raise RuntimeError(f'{name} failed:\n{report}')
                        if marker in report:
                            break
                    if process.poll() is not None:
                        raise RuntimeError(f'{name} emulator exited: {process.returncode}')
                    time.sleep(.5)
                else:
                    raise TimeoutError(f'{name}: no completed result in {args.timeout}s')
                shutil.copyfile(result_file, args.output / f'azahar-{name}-run{run}.txt')
                if not image_file.exists():
                    raise RuntimeError(f'{name}: GPU image missing')
                shutil.copyfile(image_file, args.output / f'azahar-{name}-run{run}.ppm')
                measurements = re.findall(r'draws=(\d+) CPU ([\d.]+) GPU ([\d.]+)\ntotal ([\d.]+)', report)
                if len(measurements) != 3:
                    raise RuntimeError(f'{name}: missing workload measurements')
                for draws, cpu, gpu, total in measurements:
                    results.append(dict(renderer=name, run=run, draws=int(draws), cpu_ms=float(cpu),
                                        gpu_ms=float(gpu), total_ms=float(total)))
                print(report, flush=True)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=10)

# Require identical actual displayed GPU pixels, not just matching a few samples.
images = list(args.output.glob('azahar-*-run*.ppm'))
hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in images}
if len(set(hashes.values())) != 1:
    raise RuntimeError(f'GPU images differ: {hashes}')

comparisons = []
for draws in (1, 64, 512):
    native = [r['cpu_ms'] for r in results if r['renderer'] == 'native' and r['draws'] == draws]
    reference = [r['cpu_ms'] for r in results if r['renderer'] == 'reference' and r['draws'] == draws]
    # Require the slowest native run to beat the fastest reference run.
    if max(native) >= min(reference):
        raise RuntimeError(f'CPU performance gate failed for {draws} draws: {native} vs {reference}')
    comparisons.append(dict(draws=draws, native_cpu_ms=max(native), reference_cpu_ms=min(reference),
                            reduction_percent=100*(1-max(native)/min(reference))))
(args.output / 'azahar-summary.json').write_text(json.dumps(
    dict(measurements=results, comparisons=comparisons, image_sha256=hashes,
         caveat='Emulated guest timing; not a real-3DS performance claim.'), indent=2)+'\n')
print('PASS: every acceptance run, identical GPU pixels and CPU performance gates.', flush=True)
