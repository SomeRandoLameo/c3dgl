#!/usr/bin/env python3
"""Launch exactly one emulator process and retain a fresh guest smoke report/readback."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--emulator', required=True)
parser.add_argument('--rom', type=Path, required=True)
parser.add_argument('--sdmc', type=Path, required=True)
parser.add_argument('--report', required=True, help='Guest report basename in SDMC')
parser.add_argument('--image', action='append', default=[], help='Guest image basename; can repeat')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--timeout', type=float, default=45)
args = parser.parse_args()
if args.timeout <= 0:
    parser.error('--timeout must be positive')
for name in [args.report, *args.image]:
    if Path(name).name != name:
        parser.error('Report/image names must be basenames')
args.output.mkdir(parents=True, exist_ok=True)
report = args.sdmc / args.report
old = report.stat().st_mtime_ns if report.exists() else 0
old_images = {name: (args.sdmc/name).stat().st_mtime_ns if (args.sdmc/name).exists() else 0 for name in args.image}
summary = dict(launches=1, passed=False, rom_sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest())
with (args.output/'azahar.console.log').open('w') as log:
    process = subprocess.Popen([args.emulator, str(args.rom.resolve())], stdout=log, stderr=log)
    try:
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            if report.exists() and report.stat().st_mtime_ns != old:
                text = report.read_text()
                if 'ALL CHECKS PASSED:' in text or 'FAILED:' in text or 'FAIL renderer init' in text or 'FAIL VRAM pool init' in text:
                    shutil.copyfile(report, args.output/'results.txt')
                    print(text, flush=True)
                    summary['passed'] = 'ALL CHECKS PASSED:' in text and 'FAIL' not in text
                    summary['images'] = {}
                    for name in args.image:
                        source = args.sdmc/name
                        if not source.exists() or source.stat().st_mtime_ns == old_images[name]:
                            raise RuntimeError(f'Fresh GPU image missing: {name}')
                        shutil.copyfile(source, args.output/name)
                        summary['images'][name] = hashlib.sha256(source.read_bytes()).hexdigest()
                    break
            if process.poll() is not None:
                raise RuntimeError(f'Emulator exited: {process.returncode}')
            time.sleep(.2)
        else:
            raise TimeoutError(f'Single run exceeded {args.timeout} seconds')
    except Exception as error:
        summary['passed'] = False
        summary['error'] = str(error)
        print(str(error), flush=True)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        summary['process_exit'] = process.returncode
        (args.output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
raise SystemExit(0 if summary['passed'] else 1)
