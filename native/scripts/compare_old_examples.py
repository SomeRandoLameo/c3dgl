#!/usr/bin/env python3
"""Compare selected original example functions with one run per backend and case."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--emulator', required=True)
parser.add_argument('--sdmc', required=True, type=Path)
parser.add_argument('--build', type=Path, default=Path('build-native-old-examples'))
parser.add_argument('--output', type=Path, default=Path('docs/native-results/old-examples'))
parser.add_argument('--case', action='append', choices=['primitives','texture','cube','texcoords','texmatrix','multitexture','mipmap','compressed','overlay','primitives2','primitives3','lighting'])
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
args.output.mkdir(parents=True, exist_ok=True)
cases = args.case or ['primitives','texture','cube','texcoords','texmatrix','multitexture','mipmap','compressed','overlay','primitives2','primitives3','lighting']
if len(cases) != len(set(cases)):
    parser.error('--case must not repeat')
summary = dict(repetitions=1, cases={}, whole_original_programs=False)
for case in cases:
    source = root/'examples'/('texture' if case in ('texcoords', 'texmatrix', 'multitexture', 'mipmap', 'compressed') else 'cube' if case == 'overlay' else 'primitives' if case in ('primitives2', 'primitives3') else case)/'main.cpp'
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    adapter_hash = hashlib.sha256((root/'native/compare/old_examples.cpp').read_bytes()).hexdigest()
    for backend in ['reference','native']:
        manifest = json.loads((args.build/f'old_{case}_{backend}_source.json').read_text())
        if manifest != dict(source_sha256=source_hash,adapter_sha256=adapter_hash):
            raise RuntimeError('Source/adapter changed: rebuild the comparison before running')
    entry = dict(source=str(source.relative_to(root)),source_sha256=source_hash,adapter_sha256=adapter_hash)
    for backend in ['reference','native']:
        target = args.build/f'old_{case}_{backend}.3dsx'
        destination = args.output/case/backend
        result = subprocess.run([
            sys.executable,str(root/'native/scripts/smoke_check.py'),
            '--emulator',args.emulator,'--rom',str(target),'--sdmc',str(args.sdmc),
            '--report',f'c3dgl-old-{case}-{backend}-results.txt',
            '--image',f'c3dgl-old-{case}-{backend}.ppm','--output',str(destination),
        ],check=False)
        entry[backend] = json.loads((destination/'summary.json').read_text())
        if result.returncode:
            entry['execution_failed'] = True
    if all((args.output/case/backend/f'c3dgl-old-{case}-{backend}.ppm').exists() for backend in ['reference','native']):
        images = [(args.output/case/backend/f'c3dgl-old-{case}-{backend}.ppm').read_bytes().split(b'\n255\n',1)[1] for backend in ['reference','native']]
        if len(images[0])!=400*240*3 or len(images[1])!=len(images[0]):
            raise RuntimeError('Unexpected GPU readback dimensions')
        deltas = [abs(a-b) for a,b in zip(*images)]
        pixels = [max(deltas[i:i+3]) for i in range(0,len(deltas),3)]
        entry['comparison'] = dict(exact_equal=not any(deltas),different_pixels=sum(v!=0 for v in pixels),pixels_over_one=sum(v>1 for v in pixels),max_channel_delta=max(deltas))
        print(f'{case}: {entry["comparison"]}',flush=True)
    summary['cases'][case] = entry
    (args.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')

# Missing symbol inventory is static evidence, not proof of argument/feature support.
archive = args.build/'native/cargo/armv6k-nintendo-3ds/release/libc3dgl.a'
nm = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm'
text = subprocess.run([nm,'--defined-only',str(archive)],capture_output=True,text=True,check=True).stdout
exports = set(re.findall(r'\b(gl[A-Z]\w*)$',text,re.M))
inventory = {}
for source in sorted((root/'examples').glob('*/main.*')):
    calls = set(re.findall(r'\b(gl[A-Z]\w*)\s*\(',source.read_text()))
    inventory[source.parent.name] = dict(source=str(source.relative_to(root)),missing_symbols=sorted(calls-exports),static_scan_only=True)
(args.output/'compatibility.json').write_text(json.dumps(inventory,indent=2)+'\n')
# The original included Citro3D timing functions must not survive native linkage.
for case in cases:
    text = subprocess.run([nm,str(args.build/f'old_{case}_native.elf')],capture_output=True,text=True,check=True).stdout
    if re.search(r'\b(C3D_|GPUCMD_|DVLB_|shaderProgram)\w*',text):
        raise RuntimeError(f'{case}: unexpected GPU dependency in native consumer')
summary['native_dependency_audits_passed'] = True
(args.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
raise SystemExit(0 if all(c.get('comparison',{}).get('exact_equal',False) for c in summary['cases'].values()) else 1)
