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
parser.add_argument('--frame-state', action='store_true')
parser.add_argument('--resume', action='store_true')
parser.add_argument('--emulator', required=True)
parser.add_argument('--sdmc', required=True, type=Path)
parser.add_argument('--rust3ds-rom', type=Path)
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

workloads = ('static', 'matrix', 'viewport') if args.frame_state else ('static', 'matrix')
suffixes = ('', '-matrix', '-viewport', '-state') if args.frame_state else ('', '-matrix')
expected_measurements = len(workloads)*3
measurement_pattern = r'workload=(static|matrix|viewport) draws=(\d+) CPU ([\d.]+) GPU ([\d.]+)\ntotal ([\d.]+)'
results = []
images = []
programs = [
    ('native', args.native_build / 'c3dgl_native_triangle.3dsx', 'ALL CHECKS PASSED', 'c3dgl-native'),
    ('reference', args.reference_build / 'c3dgl_reference_triangle.3dsx', 'GL error=0', 'c3dgl-reference'),
    ('native-cpp', args.native_build / 'c3dgl_native_triangle_cpp.3dsx', 'ALL CHECKS PASSED', 'c3dgl-native-cpp'),
]
if args.rust3ds_rom:
    programs.append(('rust3ds', args.rust3ds_rom, 'ALL CHECKS PASSED', 'c3dgl-rust3ds'))
rom_hashes = {name:hashlib.sha256(rom.read_bytes()).hexdigest() for name,rom,_,_ in programs}
manifest = args.output / 'rom-sha256.json'
if args.resume and manifest.exists() and json.loads(manifest.read_text())!=rom_hashes:
    raise RuntimeError('Cannot resume results with different executables')
manifest.write_text(json.dumps(rom_hashes,indent=2)+'\n')
for run in range(1, args.runs + 1):
    for name, rom, marker, stem in programs:
        if name == 'native-cpp' and run != 1:
            continue
        archived = args.output / f'azahar-{name}-run{run}.txt'
        if args.resume and archived.exists():
            report = archived.read_text()
            saved_images = [args.output / f'azahar-{name}-run{run}{suffix}.ppm' for suffix in suffixes]
            measurements = re.findall(measurement_pattern, report)
            if marker not in report or 'FAIL' in report or len(measurements)!=expected_measurements or not all(p.exists() for p in saved_images):
                raise RuntimeError(f'Incomplete saved run: {archived}')
            images.extend(saved_images)
            for workload, draws, cpu, gpu, total in measurements:
                results.append(dict(renderer=name, run=run, workload=workload, draws=int(draws),cpu_ms=float(cpu),gpu_ms=float(gpu),total_ms=float(total)))
            print(f'Reuse completed run {run}: {name}', flush=True)
            continue
        result_file = args.sdmc / (stem + '-results.txt')
        sources = [(args.sdmc / (stem + suffix + '.ppm'),suffix) for suffix in suffixes]
        image_times = {p:p.stat().st_mtime_ns if p.exists() else 0 for p,_ in sources}
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
                for source, suffix in sources:
                    if not source.exists() or source.stat().st_mtime_ns == image_times[source]:
                        raise RuntimeError(f'{name}: fresh GPU image missing: {source}')
                    destination = args.output / f'azahar-{name}-run{run}{suffix}.ppm'
                    shutil.copyfile(source, destination)
                    images.append(destination)
                measurements = re.findall(measurement_pattern, report)
                if len(measurements) != expected_measurements:
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
for suffix in suffixes:
    selected = [hashes[f'azahar-{name}-run{run}{suffix}.ppm'] for run in range(1,args.runs+1)
                for name,_,_,_ in programs if name!='native-cpp' or run==1]
    if len(set(selected))!=1: raise RuntimeError(f'GPU images differ ({suffix}): {selected}')

if hashes['azahar-native-run1.ppm'] == hashes['azahar-native-run1-matrix.ppm']:
    raise RuntimeError('Matrix workload did not change the image')

comparisons = []
for workload, draws in ((w,d) for w in workloads for d in (1,64,512)):
    native = [r['cpu_ms'] for r in results if r['renderer'] == 'native' and r['draws'] == draws and r['workload'] == workload]
    reference = [r['cpu_ms'] for r in results if r['renderer'] == 'reference' and r['draws'] == draws and r['workload'] == workload]
    # Require the slowest native run to beat the fastest reference run.
    if max(native) >= min(reference):
        raise RuntimeError(f'CPU performance gate failed for {draws} draws: {native} vs {reference}')
    comparisons.append(dict(workload=workload, draws=draws, native_cpu_ms=max(native), reference_cpu_ms=min(reference),
                            reduction_percent=100*(1-max(native)/min(reference))))
rust3ds_comparisons = []
if args.rust3ds_rom:
    for workload, draws in ((w,d) for w in workloads for d in (1,64,512)):
        rust = [r['cpu_ms'] for r in results if r['renderer'] == 'rust3ds' and r['draws']==draws and r['workload']==workload]
        reference = [r['cpu_ms'] for r in results if r['renderer']=='reference' and r['draws']==draws and r['workload']==workload]
        if max(rust)>=min(reference): raise RuntimeError(f'rust3ds CPU gate failed: {workload}/{draws}')
        rust3ds_comparisons.append(dict(workload=workload,draws=draws,rust3ds_cpu_ms=max(rust),reference_cpu_ms=min(reference),reduction_percent=100*(1-max(rust)/min(reference))))
baseline_changes = []
rust3ds_baseline_changes = []
if args.baseline_summary.exists():
    prior = json.loads(args.baseline_summary.read_text())
    for comparison in comparisons:
        previous = next((c for c in prior['comparisons']
                         if c['draws'] == comparison['draws'] and c.get('workload', 'static') == comparison['workload']), None)
        if previous is None: continue
        baseline_changes.append(dict(workload=comparison['workload'], draws=comparison['draws'],
            previous_native_cpu_ms=previous['native_cpu_ms'],
            current_native_cpu_ms=comparison['native_cpu_ms'],
            change_percent=100*(comparison['native_cpu_ms']/previous['native_cpu_ms']-1)))
    for comparison in rust3ds_comparisons:
        previous = next((c for c in prior.get('rust3ds_comparisons', [])
                         if c['draws']==comparison['draws'] and c['workload']==comparison['workload']),None)
        if previous is not None:
            rust3ds_baseline_changes.append(dict(workload=comparison['workload'],draws=comparison['draws'],
                previous_rust3ds_cpu_ms=previous['rust3ds_cpu_ms'],current_rust3ds_cpu_ms=comparison['rust3ds_cpu_ms'],
                change_percent=100*(comparison['rust3ds_cpu_ms']/previous['rust3ds_cpu_ms']-1)))
(args.output / 'azahar-summary.json').write_text(json.dumps(
    dict(rom_sha256=rom_hashes, measurements=results, comparisons=comparisons, image_sha256=hashes,
         rust3ds_comparisons=rust3ds_comparisons, rust3ds_baseline_changes=rust3ds_baseline_changes, baseline_summary=str(args.baseline_summary), baseline_changes=baseline_changes,
         caveat='Emulated guest timing; not a real-3DS performance claim.'), indent=2)+'\n')
print('PASS: every acceptance run, identical GPU pixels and CPU performance gates.', flush=True)
