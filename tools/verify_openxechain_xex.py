#!/usr/bin/env python3
import struct
import sys
from pathlib import Path

if len(sys.argv) != 3 or sys.argv[2] not in {'title', 'sysdll'}:
    raise SystemExit('usage: verify_openxechain_xex.py <file.xex> <title|sysdll>')

p = Path(sys.argv[1])
kind = sys.argv[2]
data = p.read_bytes()
if len(data) < 24 or data[:4] != b'XEX2':
    raise SystemExit('XEX verify FAIL: missing XEX2 header')
flags = struct.unpack_from('>I', data, 4)[0]
expected = 0x00000001 if kind == 'title' else 0x0000000A
if flags != expected:
    raise SystemExit('XEX verify FAIL: expected %s flags 0x%X, got 0x%08X' % (kind, expected, flags))
print('PASS XEX2 magic')
print('PASS %s module flags 0x%08X' % (kind, flags))
print('OpenXeChain XEX verifier: PASS')
'OpenXeChain XEX verifier: PASS')
