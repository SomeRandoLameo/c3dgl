#!/usr/bin/env python3
"""Assemble the built-in shader and retain only code and operand descriptors."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--check', action='store_true')
parser.add_argument('--picasso', default=os.environ.get('DEVKITPRO', '/opt/devkitpro') + '/tools/bin/picasso')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='c3dgl-shader-') as directory:
    binary = Path(directory) / 'triangle.shbin'
    subprocess.run([args.picasso, '-o', str(binary), str(root/'shaders/triangle.v.pica')], check=True)
    data = binary.read_bytes()
    if len(data) % 4:
        raise ValueError('Shader binary is not word aligned')
    words = struct.unpack('<' + 'I' * (len(data)//4), data)
    if words[0] != 0x424c5644 or words[1] != 1:
        raise ValueError('Expected a single DVLE')
    program = 3
    entry = words[2]//4
    if words[program] != 0x504c5644 or words[entry] != 0x454c5644:
        raise ValueError('Unexpected shader tables')
    code_offset = program + words[program+2]//4
    code = list(words[code_offset:code_offset+words[program+3]])
    descriptors = [words[program+words[program+4]//4+i*2] for i in range(words[program+5])]
    generated = ('// Generated from shaders/triangle.v.pica with devkitPro picasso.\n'
                 f'pub const ENTRY: u32 = {words[entry+2]};\n'
                 f'pub const CODE: &[u32] = &{code};\n'
                 f'pub const DESCRIPTORS: &[u32] = &{descriptors};\n')
    source = root/'src/shader.rs'
    if args.check:
        if source.read_text() != generated:
            raise RuntimeError('src/shader.rs differs from the assembled source')
        print('PASS: embedded shader matches PICA source.')
    else:
        source.write_text(generated)
