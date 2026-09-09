#!/usr/bin/env python3
import struct
import sys
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit('usage: verify_openxechain_xex.py <file.xex>')
p = Path(sys.argv[1])
data = p.read_bytes()
if len(data) < 24 or data[:4] != b'XEX2':
    raise SystemExit('XEX verify FAIL: missing XEX2 header')
flags = struct.unpack_from('>I', data, 4)[0]
# SynthXEX --type sysdll sets EXPORTS (0x2) + DLL (0x8). EXPORTS here is the
# XEX module-type flag; this project does not publish a user export table.
required = 0x0000000A
if (flags & required) != required:
    raise SystemExit('XEX verify FAIL: expected sysdll flags 0xA, got 0x%08X' % flags)
print('PASS XEX2 magic')
print('PASS sysdll module flags 0x%08X' % flags)
print('OpenXeChain XEX verifier: PASS')
