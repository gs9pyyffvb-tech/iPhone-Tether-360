#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
scan_roots = [root / 'src', root / 'build_openxechain.sh', root / '.github',
              root / 'README.md', root / 'HARDWARE_TEST.md', root / 'VALIDATION.md',
              root / 'BATCH9C_RESULT.md', root / 'OPENXECHAIN_PORT_AUDIT.md',
              root / 'launch.ini.example']
patterns = {
    'legacy xtl header': re.compile(r'(?i)\bxtl\.h\b'),
    'legacy SDK/toolchain reference': re.compile(r'\b(?:XDK|XEDK)\b'),
    'legacy XEX packer': re.compile(r'(?i)\bimagexex(?:\.exe)?\b'),
    'legacy import library': re.compile(r'(?i)\b(?:xapilib|xboxkrnl)\.lib\b'),
    'legacy compiler entry': re.compile(r'\b_DllMainCRTStartup\b'),
    'legacy runtime thread startup': re.compile(r'\bXapiThreadStartup\b'),
    'legacy thread wrapper': re.compile(r'\bCreateThread\s*\('),
    'legacy sleep wrapper': re.compile(r'\bSleep\s*\('),
    'legacy atomic wrapper': re.compile(r'\bInterlocked(?:Exchange|CompareExchange|Increment)\s*\('),
    'MSVC allocation directive': re.compile(r'__declspec\s*\('),
    'MSVC section pragma': re.compile(r'#\s*pragma\s+section\b'),
}

files = []
for item in scan_roots:
    if item.is_file(): files.append(item)
    elif item.exists():
        files.extend(p for p in item.rglob('*') if p.is_file() and p.suffix.lower() in {'.cpp','.h','.sh','.yml','.yaml','.md','.example'})

failures = []
for path in files:
    text = path.read_text(errors='replace')
    rel = path.relative_to(root)
    for label, rx in patterns.items():
        for m in rx.finditer(text):
            # OpenXeChain's platform implementation deliberately invokes the OS
            # ExCreateThread symbol; the banned expression only matches CreateThread.
            line = text.count('\n', 0, m.start()) + 1
            failures.append(f'{rel}:{line}: {label}: {m.group(0)}')

required = [
    ('src/platform/xbox_platform.h', 'IT360_OPENXECHAIN'),
    ('src/platform/xbox_platform.cpp', 'ExCreateThread'),
    ('src/platform/xbox_platform.cpp', 'KeDelayExecutionThread'),
    ('src/platform/xbox_platform.cpp', 'XexGetProcedureAddress'),
    ('src/platform/xbox_platform.cpp', 'CreateFileA'),
    ('src/plugin.cpp', 'extern "C" int DllMain(unsigned int'),
    ('build_openxechain.sh', '--target=ppc32-xbox360'),
    ('build_openxechain.sh', '/subsystem:xbox360'),
    ('build_openxechain.sh', '--type sysdll'),
]
for rel, needle in required:
    if needle not in (root / rel).read_text():
        failures.append(f'{rel}: missing required OpenXeChain marker: {needle}')

if failures:
    print('OpenXeChain migration audit: FAIL', file=sys.stderr)
    for f in failures: print('  ' + f, file=sys.stderr)
    sys.exit(1)
print(f'OpenXeChain migration audit: PASS ({len(files)} source/build files scanned)')
