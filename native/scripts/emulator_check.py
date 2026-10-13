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
parser.add_argument('--baseline-summary', type=Path, default=Path('docs/native-results/azahar-summary.json'))
parser.add_argument('--runs', default=3, type=int)
parser.add_argument('--timeout', default=180, type=float)
args = parser.parse_args()
if args.runs < 1:
    parser.error('--runs must be positive')
args.output.mkdir(parents=True, exist_ok=True)

results = []
images = []
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
        matrix_image = args.sdmc / (stem + '-matrix.ppm')
        image_times = {p: p.stat().st_mtime_ns if p.exists() else 0 for p in (image_file, matrix_image)}
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
                for source, suffix in ((image_file, ''), (matrix_image, '-matrix')):
                    if not source.exists() or source.stat().st_mtime_ns == image_times[source]:
                        raise RuntimeError(f'{name}: fresh GPU image missing: {source}')
                    destination = args.output / f'azahar-{name}-run{run}{suffix}.ppm'
                    shutil.copyfile(source, destination)
                    images.append(destination)
                measurements = re.findall(r'workload=(static|matrix) draws=(\d+) CPU ([\d.]+) GPU ([\d.]+)\ntotal ([\d.]+)', report)
                if len(measurements) != 6:
                    raise RuntimeError(f'{name}: missing workload measurements')
                for workload, draws, cpu, gpu, total in measurements:
                    results.append(dict(renderer=name, run=run, workload=workload, draws=int(draws), cpu_ms=float(cpu),
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
hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in images}
if any(len({v for k,v in hashes.items() if ('-matrix.ppm' in k)==matrix}) != 1 for matrix in (False,True)):
    raise RuntimeError(f'GPU images differ: {hashes}')

if hashes['azahar-native-run1.ppm'] == hashes['azahar-native-run1-matrix.ppm']:
    raise RuntimeError('Matrix workload did not change the image')

comparisons = []
for workload, draws in ((w,d) for w in ("static","matrix") for d in (1,64,512)):
    native = [r['cpu_ms'] for r in results if r['renderer'] == 'native' and r['draws'] == draws and r['workload'] == workload]
    reference = [r['cpu_ms'] for r in results if r['renderer'] == 'reference' and r['draws'] == draws and r['workload'] == workload]
    # Require the slowest native run to beat the fastest reference run.
    if max(native) >= min(reference):
        raise RuntimeError(f'CPU performance gate failed for {draws} draws: {native} vs {reference}')
    comparisons.append(dict(workload=workload, draws=draws, native_cpu_ms=max(native), reference_cpu_ms=min(reference),
                            reduction_percent=100*(1-max(native)/min(reference))))
baseline_changes = []
if args.baseline_summary.exists():
    prior = json.loads(args.baseline_summary.read_text())
    for comparison in comparisons:
        if comparison['workload'] != 'static': continue
        previous = next(c for c in prior['comparisons']
                        if c['draws'] == comparison['draws'] and c.get('workload', 'static') == 'static')
        baseline_changes.append(dict(draws=comparison['draws'],
            previous_native_cpu_ms=previous['native_cpu_ms'],
            current_native_cpu_ms=comparison['native_cpu_ms'],
            change_percent=100*(comparison['native_cpu_ms']/previous['native_cpu_ms']-1)))
(args.output / 'azahar-summary.json').write_text(json.dumps(
    dict(measurements=results, comparisons=comparisons, image_sha256=hashes,
         baseline_summary=str(args.baseline_summary), baseline_changes=baseline_changes,
         caveat='Emulated guest timing; not a real-3DS performance claim.'), indent=2)+'\n')
print('PASS: every acceptance run, identical GPU pixels and CPU performance gates.', flush=True)
