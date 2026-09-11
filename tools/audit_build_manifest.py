#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = (ROOT / 'build_openxechain.sh').read_text()
WORKFLOW = (ROOT / '.github/workflows/build-xex.yml').read_text()
LAUNCH = (ROOT / 'launch.ini.example').read_text()

checks = []
def check(ok, name):
    checks.append((bool(ok), name))

def array(name):
    m = re.search(r'^%s=\(\n(.*?)^\)' % re.escape(name), BUILD, re.M | re.S)
    if not m:
        return []
    return [x.strip() for x in m.group(1).splitlines() if x.strip() and not x.lstrip().startswith('#')]

targets = {name: array(name) for name in ('LOADER_SOURCES','LICENSEID_SOURCES','CORE_SOURCES')}
for name, paths in targets.items():
    check(bool(paths), '%s exists and is non-empty' % name)
    check(len(paths) == len(set(paths)), '%s contains no duplicate translation units' % name)
    check(all(not p.startswith('/') and '..' not in Path(p).parts for p in paths), '%s contains repository-relative paths only' % name)
    check(all(p.endswith('.cpp') for p in paths), '%s contains only C++ translation units' % name)
    missing = [p for p in paths if not (ROOT / p).is_file()]
    check(not missing, '%s lists only existing files%s' % (name, (': ' + ', '.join(missing)) if missing else ''))

listed = set().union(*[set(v) for v in targets.values()])
all_cpp = {p.relative_to(ROOT).as_posix() for p in (ROOT/'src').rglob('*.cpp')}
check(all_cpp == listed,
      'every project translation unit belongs to at least one build target (missing=%s extra=%s)' %
      (sorted(all_cpp-listed), sorted(listed-all_cpp)))

# Resolve project-local quoted includes. BearSSL is the only intentionally external quoted header.
external_quoted = {'bearssl.h'}
missing_headers = []
for f in sorted((ROOT/'src').rglob('*')):
    if not f.is_file() or f.suffix not in ('.cpp','.h'):
        continue
    text = f.read_text(errors='replace')
    for inc in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M):
        if inc in external_quoted:
            continue
        candidates = [f.parent/inc, ROOT/'src'/inc, ROOT/inc]
        if not any(c.is_file() for c in candidates):
            missing_headers.append('%s -> %s' % (f.relative_to(ROOT), inc))
check(not missing_headers, 'all quoted project headers resolve%s' % ((': ' + '; '.join(missing_headers)) if missing_headers else ''))

active_files = [ROOT/'build_openxechain.sh', ROOT/'.github/workflows/build-xex.yml', ROOT/'launch.ini.example']
active_files += sorted((ROOT/'src').rglob('*.cpp')) + sorted((ROOT/'src').rglob('*.h'))
active_text = '\n'.join(p.read_text(errors='replace') for p in active_files)
check('/mnt/data' not in active_text and 'step4work' not in active_text and 'FINAL-app-step' not in active_text,
      'active build/source contains no local reconstruction path leakage')
check(not re.search(r'#\s*include\s*[<"](?:xtl|xbox|winsockx)\.h[>"]', active_text, re.I),
      'active source contains no Microsoft XDK header includes')
check('__declspec' not in active_text and '_declspec' not in active_text,
      'active source contains no Microsoft compiler declspec dependency')

check('iPhoneTether360-B9C-OXC.xex' not in BUILD, 'active build script contains no obsolete B9C output alias')
check('iPhoneTether360-B9C-OXC.xex' not in WORKFLOW, 'workflow contains no obsolete B9C artifact')
check('iPhoneTether360-B9C-OXC.xex' not in LAUNCH, 'deployment example contains no obsolete B9C artifact')

check('python3 "$ROOT/tools/verify_openxechain_pe.py" "$pe" title' in BUILD and
      'python3 "$ROOT/tools/verify_openxechain_xex.py" "$xex" title' in BUILD,
      'Loader/LicenseID title builds run PE and XEX structural verification')
check('python3 "$ROOT/tools/verify_openxechain_pe.py" "$CORE_PE" sysdll' in BUILD and
      'python3 "$ROOT/tools/verify_openxechain_xex.py" "$CORE_XEX" sysdll' in BUILD,
      'Core sysdll build runs PE and XEX structural verification')
check('link_title Loader' in BUILD and 'link_title LicenseID' in BUILD and '--type sysdll' in BUILD,
      'build emits two title XEXs and one sysdll XEX')
check(all(('build/'+n) in WORKFLOW for n in (
      'Loader.xex','Loader.sha256','Core.xex','Core.sha256','LicenseID.xex','LicenseID.sha256','BUILD_OUTPUTS.txt','VERSION.txt')),
      'workflow uploads all final deployment/build metadata files')
check('python3 tools/audit_build_manifest.py' in WORKFLOW and
      WORKFLOW.index('python3 tools/audit_build_manifest.py') < WORKFLOW.index('Build final XEX set'),
      'build-manifest audit runs before expensive final XEX build')
check('python3 tools/audit_step4.py' in WORKFLOW and
      WORKFLOW.index('python3 tools/audit_step4.py') < WORKFLOW.index('Build final XEX set'),
      'Step-4 audit runs before expensive final XEX build')
check('Core.xex' in LAUNCH, 'launch example points at resident Core.xex')
host_validation=(ROOT/'tools/run_host_validation.sh').read_text()
check('bash tools/audit_host_symbols.sh' in host_validation and 'bash tools/test_notify_openxechain_syntax.sh' in host_validation,
      'host gate includes per-target symbol audit and OpenXeChain-only notification syntax check')

failed=[n for ok,n in checks if not ok]
for ok,n in checks:
    print(('PASS ' if ok else 'FAIL ') + n)
if failed:
    print('Build manifest audit: FAIL (%d/%d failed)' % (len(failed),len(checks)), file=sys.stderr)
    sys.exit(1)
print('Build manifest audit: PASS (%d checks)' % len(checks))
