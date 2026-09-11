#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    p = ROOT / rel
    return p.read_text(errors="replace") if p.is_file() else ""

checks = []
def require(name, condition):
    checks.append((name, bool(condition)))

early = read("src/early_trace.cpp")
header = read("src/early_trace.h")
entry = read("src/entry.cpp")
build = read("build_openxechain.sh")

# First-instruction boundary: the trace call must precede CRT initialization.
first_trace = entry.find("it360_early_trace::BeginSessionLiteral")
crt = entry.find("crtinit();", first_trace)
require("ENTRY 00 trace precedes crtinit", first_trace >= 0 and crt >= 0 and first_trace < crt)
require("ENTRY 00 identifies pre-CRT boundary", "_start reached before CRT" in entry)

# Emergency writer must be usable without app-directory resolution or CRT file wrappers.
require("emergency native HDD path retained", r'\\Device\\Harddisk0\\Partition1\\iPhoneTether360.log' in early)
require("emergency visible HDD path retained", r'Hdd1:\\iPhoneTether360.log' in early)
require("native NtCreateFile used", "NtCreateFile(" in early)
require("native NtWriteFile used", "NtWriteFile(" in early)
require("native NtFlushBuffersFile used", "NtFlushBuffersFile(" in early)
require("native NtClose used", "NtClose(" in early)
require("append-only synchronous open", "FILE_APPEND_DATA | SYNCHRONIZE" in early)
require("open-or-create semantics", "FILE_OPEN_IF" in early)
require("synchronous non-alert file I/O", "FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE" in early)
require("early path avoids CreateFileA wrapper", "CreateFileA(" not in early)
require("early path avoids heap allocation", all(token not in early for token in ["malloc(", "calloc(", "realloc(", "new "]))

# RAM replay means an emergency-file failure does not destroy the early checkpoints.
require("bounded pre-handoff replay buffer", "kReplayCapacity = 8192u" in early and "gReplay" in early)
require("pre-handoff writes are captured", "CaptureReplay(data, length, append_crlf)" in early)
require("BeginSession still records line when separator persistence fails", "const bool separator_ok" in early and "const bool line_ok = WriteLiteral(text)" in early)
require("replay is copied to normal Boot.log", "ReplayEmergencyIntoNormalUnlocked" in early)
require("replay overflow is explicitly reported", "replay buffer overflowed" in early)

# Handoff must occur only after the normal app-local paths exist.
resolve = early.find("ResolveAppDirectory")
join_boot = early.find('AppendLeaf(gAppDirectory, "Boot.log"')
normal_open = early.find("EnsureNormalOpenUnlocked")
replay = early.find("ReplayEmergencyIntoNormalUnlocked", normal_open + 1)
normal_ready = early.find("AtomicExchange(&gNormalReady, 1)", replay + 1)
require("normal Boot.log remains app-local", join_boot >= 0)
require("runtime Log.txt remains app-local", 'AppendLeaf(gAppDirectory, "Log.txt"' in early)
require("normal path resolution precedes handoff", resolve >= 0 and join_boot > resolve)
require("normal Boot.log opens before replay", normal_open >= 0 and replay > normal_open)
require("replay completes before normal routing enabled", replay >= 0 and normal_ready > replay)
require("emergency handle is closed during handoff", "NativeClose(&gEmergencyFile)" in early)
require("handoff marker is written to emergency trace", "EARLY HANDOFF: switching future checkpoints" in early)
require("normal handoff marker confirms replay and flush", "pre-CRT trace replayed and flushed" in early)

# Crash survivability: every native persistence operation finishes with a flush.
require("native persistence flushes after writes", "ok = NativeFlush(file, &status)" in early)
require("failed normal handoff stays on emergency route", "emergency HDD trace remains active" in early)
require("normal readiness is separately observable", "NormalBootLogReady()" in early and "NormalBootLogReady();" in header)
require("emergency path is separately observable", "EmergencyVisiblePath()" in early and "EmergencyVisiblePath();" in header)

# Keep the build aware of this audit once the final OpenXeChain build is attempted.
require("Step 2 audit included by build script", "tools/audit_step2.py" in build)

failed = [name for name, ok in checks if not ok]
for name, ok in checks:
    print(("PASS" if ok else "FAIL") + " | " + name)
print("STEP2 BOOT LOG AUDIT %d/%d" % (len(checks) - len(failed), len(checks)))
if failed:
    sys.exit(1)
