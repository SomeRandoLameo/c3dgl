#!/usr/bin/env python3
"""Sampling profiler for a 3DS ROM in Azahar through its GDB stub.

Azahar counts about one tick per ARM instruction, so the share of PC samples per function is the share of
emulated CPU time. Each sample interrupts the guest, reads PC and LR and continues. PCs are resolved with
addr2line (inline chains need line tables: the release profile keeps them).

The GDB stub is switched on for this launch only: qt-config.ini is backed up, edited, and restored once the
emulator has read it. The emulator process started here is killed afterwards; no other instance is touched.
"""
import argparse
import collections
from pathlib import Path
import shutil
import socket
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--emulator', default='/Applications/Azahar.app/Contents/MacOS/azahar')
parser.add_argument('--rom', type=Path, required=True)
parser.add_argument('--elf', type=Path, help='defaults to the ROM with .elf')
parser.add_argument('--config', type=Path,
                    default=Path.home() / 'Library/Application Support/Azahar/config/qt-config.ini')
parser.add_argument('--port', type=int, default=24689)
parser.add_argument('--skip', type=float, default=3, help='seconds to run before sampling (setup, warmup)')
parser.add_argument('--seconds', type=float, default=20)
parser.add_argument('--interval', type=float, default=0.004)
parser.add_argument('--top', type=int, default=40)
args = parser.parse_args()
elf = args.elf or args.rom.with_suffix('.elf')
if not args.rom.is_file() or not elf.is_file():
    parser.error(f'missing ROM or ELF: {args.rom}, {elf}')
addr2line = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-addr2line'


def packet(data):
    return b'$' + data + b'#' + b'%02x' % (sum(data) & 255)


class Stub:
    def __init__(self, port):
        deadline = time.monotonic() + 90
        while True:
            try:
                self.s = socket.create_connection(('127.0.0.1', port), timeout=10)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(.2)
        self.buf = b''

    def read_packet(self):
        while True:
            start = self.buf.find(b'$')
            end = self.buf.find(b'#', start)
            if start >= 0 and end >= 0 and len(self.buf) >= end + 3:
                data = self.buf[start + 1:end]
                self.buf = self.buf[end + 3:]
                self.s.sendall(b'+')
                return data
            chunk = self.s.recv(65536)
            if not chunk:
                raise EOFError
            self.buf += chunk

    def command(self, data):
        self.s.sendall(packet(data))
        return self.read_packet()

    def registers(self):
        regs = self.command(b'g').decode()
        word = lambda i: int.from_bytes(bytes.fromhex(regs[i * 8:i * 8 + 8]), 'little')
        return word(15), word(14)


backup = args.config.with_suffix('.ini.profile-backup')
shutil.copyfile(args.config, backup)
text = args.config.read_text()
text = text.replace('use_gdbstub=false', 'use_gdbstub=true').replace('use_gdbstub\\default=true',
                                                                       'use_gdbstub\\default=false')
args.config.write_text(text)
process = subprocess.Popen([args.emulator, str(args.rom.resolve())], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
samples = []
try:
    stub = Stub(args.port)
    time.sleep(1)
    shutil.copyfile(backup, args.config)
    stub.command(b'?')
    stub.s.sendall(packet(b'c'))
    time.sleep(args.skip)
    end = time.monotonic() + args.seconds
    while time.monotonic() < end:
        stub.s.sendall(b'\x03')
        stub.read_packet()
        samples.append(stub.registers())
        stub.s.sendall(packet(b'c'))
        time.sleep(args.interval)
finally:
    if backup.exists():
        shutil.copyfile(backup, args.config)
        backup.unlink()
    process.kill()
    process.wait()

addresses = sorted({a for pc, lr in samples for a in (pc, lr)})
resolved = {}
out = subprocess.run([addr2line, '-f', '-i', '-C', '-a', '-e', str(elf)] + ['%x' % a for a in addresses],
                     capture_output=True, text=True).stdout.splitlines()
current = None
for line in out:
    if line.startswith('0x'):
        current = int(line, 16)
        resolved[current] = []
    elif current is not None and not line.startswith('/') and ':' not in line[:2]:
        if not (line.startswith('??') and resolved[current]):
            resolved[current].append(line)
    elif current is not None and resolved[current]:
        resolved[current][-1] += '  ' + line.rsplit('/', 1)[-1]

def frames(a):
    chain = resolved.get(a) or ['??']
    return chain

flat, inclusive, outer, callers = (collections.Counter() for _ in range(4))
for pc, lr in samples:
    chain = frames(pc)
    flat[chain[0]] += 1
    outer[chain[-1]] += 1
    for f in set(chain):
        inclusive[f] += 1
    callers[(chain[-1], frames(lr)[-1])] += 1
n = len(samples)
print(f'{n} samples')
for title, counter in [('innermost (inlined) frame', flat), ('inclusive over inline chain', inclusive),
                       ('outer (real) function', outer), ('outer function <- caller (LR)', callers)]:
    print(f'\n== {title}')
    for name, count in counter.most_common(args.top):
        label = ' <- '.join(name) if isinstance(name, tuple) else name
        print(f'{100 * count / n:6.2f}%  {label[:200]}')
