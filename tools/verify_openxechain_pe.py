#!/usr/bin/env python3
import struct
import sys
from pathlib import Path

if len(sys.argv) != 3 or sys.argv[2] not in {'title', 'sysdll'}:
    raise SystemExit('usage: verify_openxechain_pe.py <file.exe> <title|sysdll>')

p = Path(sys.argv[1])
kind = sys.argv[2]
data = p.read_bytes()
if len(data) < 0x100 or data[:2] != b'MZ':
    raise SystemExit('PE verify FAIL: missing MZ header')
peoff = struct.unpack_from('<I', data, 0x3C)[0]
if peoff + 0x78 > len(data) or data[peoff:peoff+4] != b'PE\0\0':
    raise SystemExit('PE verify FAIL: missing PE header')
machine, sections, _, _, _, opt_size, characteristics = struct.unpack_from('<HHIIIHH', data, peoff + 4)
opt = peoff + 24
magic = struct.unpack_from('<H', data, opt)[0]
if magic != 0x10B:
    raise SystemExit('PE verify FAIL: expected PE32 optional header')
entry = struct.unpack_from('<I', data, opt + 16)[0]
image_base = struct.unpack_from('<I', data, opt + 28)[0]
section_alignment = struct.unpack_from('<I', data, opt + 32)[0]
subsystem = struct.unpack_from('<H', data, opt + 68)[0]
is_dll = bool(characteristics & 0x2000)
expected_base = 0x82000000 if kind == 'title' else 0x91DE0000
checks = {
    'POWERPCBE machine 0x1F2': machine == 0x1F2,
    'at least one section': sections > 0,
    ('title is not PE DLL' if kind == 'title' else 'sysdll has PE DLL characteristic'): (not is_dll if kind == 'title' else is_dll),
    'non-zero entry point': entry != 0,
    'expected image base 0x%08X' % expected_base: image_base == expected_base,
    'SynthXEX-compatible page alignment': section_alignment in (0x1000, 0x10000),
    'Xbox subsystem 0x000E': subsystem == 0x000E,
    'optional header fits': peoff + 24 + opt_size <= len(data),
}
failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(('PASS ' if ok else 'FAIL ') + name)
if failed:
    raise SystemExit('PE verify FAIL: ' + ', '.join(failed))
print('OpenXeChain PE verifier: PASS (%s)' % kind)
