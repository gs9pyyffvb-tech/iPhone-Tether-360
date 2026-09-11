#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
checks = []

def text(path):
    p = ROOT / path
    if not p.is_file():
        return ""
    return p.read_text(errors="replace")

def require(name, condition):
    checks.append((name, bool(condition)))

version = text("src/shared/version.h")
diag_h = text("src/diag.h")
diag = text("src/diag.cpp")
notify = text("src/shared/notify.cpp")
tether = text("src/tether_driver.cpp")
runtime = text("src/license/license_runtime.cpp")
plugin = text("src/plugin.cpp")
loader = text("src/loader/loader_main.cpp")
licenseid = text("src/licenseid/licenseid_main.cpp")
early = text("src/early_trace.cpp")
build = text("build_openxechain.sh")
targets = text("TARGETS.md")
bootlog = text("src/shared/boot_log.cpp")

# Version/release identity.
require("version is 1.0.0", '#define IT360_VERSION_STRING "1.0.0"' in version)
require("product version constant", 'IT360_PRODUCT_VERSION "iPhoneTether360 1.0.0"' in version)
require("build version is 1.0.0", 'APP_VERSION="1.0.0"' in build)
require("build emits VERSION.txt", 'VERSION.txt' in build)
require("targets identify 1.0.0", "iPhoneTether360 1.0.0 target layout" in targets)
require("Loader Boot.log carries version", 'IT360_PRODUCT_VERSION " Loader session"' in loader)
require("Core Boot.log carries version", 'IT360_VERSION_STRING " Core diagnostic session' in early)
require("Core runtime log carries version", 'IT360_VERSION_STRING' in diag)

# Notification vocabulary.
for message in [
    "iPhoneTether360 Ready",
    "iPhone Detected",
    "Checking iPhone Data...",
    "iPhone Data Working",
    "iPhone Data Not Working",
    "License Has Been Found",
    "No License found for this Xbox",
    "iPhone to Xbox Complete",
    "Reconnecting iPhone...",
    "iPhone Disconnected",
]:
    require("notification text: " + message, message in (diag + tether + runtime + plugin + notify))

# Queue/readability and stale-state handling.
require("2.5 second queued notification gap", "kNotifyGapMs = 2500ULL" in diag)
require("title notifier has no hidden timing layer", "kDirectNotifyGapMs" not in notify and "WaitForReadableGap" not in notify)
require("title notifier uses direct standard XNotify tuple", "ShowTitle" in notify and "notify(14u, 0u, 2u" in notify)
require("Core notifier has explicit system-thread path", "ShowSystem" in notify and "SystemNotifyThread" in notify)
require("phone notices carry session id", "phoneConnection" in diag and "BeginPhoneNotifications" in diag_h)
require("phone disconnect invalidates pending notices", "EndPhoneNotifications" in tether and "RemovePhoneNotificationsUnlocked" in diag)
require("stale phone notices dropped before display", "const bool stale" in diag and "slot->phoneConnection != active" in diag)
require("temporary loss resets progress only", "ResetPhoneProgressNotifications" in tether and "progressMask" in diag)
require("queued stale progress is removed", "RemovePhoneProgressUnlocked(connection_id)" in diag)
require("data recovery removes queued not-working state", "queued.kind == PhoneNoticeDataNotWorking" in diag)
require("per-session duplicate suppression", "gPhoneNoticeSeenMask" in diag)
require("old IP-address popup removed", "iPhone USB network has an IP address" not in tether)
require("old hotspot popup removed", "iPhone Personal Hotspot connected" not in tether)
require("old validation popup removed", "iPhone USB Internet path validated" not in tether)

# Success/failure sequencing.
require("detected notification begins after phone session id", tether.find("BeginPhoneNotifications(gCtx.connectionId)") < tether.find('"iPhone Detected"'))
require("checking data occurs when validation starts", "PhoneNoticeCheckingData" in tether and "gNet.Start" in tether)
require("plain HTTP proof marks data working", "AtomicExchange(&gInternetProbeOk, 1)" in tether)
require("HTTPS proof also marks data working", "AtomicExchange(r.independent_internet_ok, 1)" in runtime)
require("HTTPS queues data-working before verdict", runtime.find("PhoneNoticeDataWorking") < runtime.find("EvaluateLicenceDocument"))
require("licence found is queued", "PhoneNoticeLicenceFound" in runtime)
require("no licence is queued", "PhoneNoticeNoLicence" in runtime)
require("not-working is queued on no Internet", "PhoneNoticeDataNotWorking" in runtime)
require("Complete requires data proof", "gInternetProbeOk" in tether[tether.find("if (!gCtx.completeNotified"):tether.find("// Drain XNet", tether.find("if (!gCtx.completeNotified"))])
require("Complete waits for licence worker", "!it360_license_runtime::CheckActive()" in tether)
require("Complete requires licence gate allow", "it360_license_runtime::NativeNetworkAllowed()" in tether)
require("Complete requires B9D active", "it360_native_net::IsActive()" in tether)
require("Complete is never emitted from licence runtime", "iPhone to Xbox Complete" not in runtime)
require("disconnect notification after invalidation", tether.find("EndPhoneNotifications(connection_id)") < tether.find('Notify("iPhone Disconnected")'))
require("reconnecting requires prior working data", "gCtx.hadWorkingData && !gCtx.reconnectingNotified" in tether)
require("reconnect clears Internet proof", "AtomicExchange(&gInternetProbeOk, 0)" in tether)

# Runtime log policy.
require("Log.txt reset each Core session", "OpenTruncate(runtimePath)" in diag)
require("log reset happens in Init", diag.find("OpenTruncate(runtimePath)") < diag.find("StartDetachedThread(Worker"))
require("runtime lines have elapsed timestamp", '"[+%010llu ms] "' in diag)
require("timestamp uses monotonic time", "MonotonicMs()" in diag)
require("Boot.log append stays enabled", "FILE_APPEND_DATA" in early or "FILE_APPEND_DATA" in bootlog or "OpenAppend" in bootlog)
require("runtime log remains app-local", 'AppendLeaf(gAppDirectory, "Log.txt"' in early)
require("Boot.log remains app-local", 'AppendLeaf(gAppDirectory, "Boot.log"' in early)

# LicenseID presentation/persistence.
require("LicenseID truncates old output", "OpenTruncate(path)" in licenseid)
require("LicenseID writes only 64-char hash", "WriteAll(file, id, 64u)" in licenseid)
require("LicenseID title notification", 'ShowTitle("iPhoneTether360 License ID")' in licenseid)
require("LicenseID full ID notification", '"License ID: "' in licenseid)
require("LicenseID saved notification", 'ShowTitle("Saved to LicenseID.txt")' in licenseid)
require("LicenseID direct notifications are explicitly spaced", licenseid.count("SleepMs(2500u)") == 2)

# Privacy and v1 simplicity.
for path in ["src/diag.cpp", "src/plugin.cpp", "src/tether_driver.cpp", "src/shared/notify.cpp", "src/loader/loader_main.cpp"]:
    body = text(path)
    require("licence endpoint absent from " + path,
            "raw.githubusercontent.com" not in body and "rghmodder1991" not in body and "License360" not in body)
require("no config wizard introduced", "config.ini" not in loader + plugin + tether)
require("Core remains resident architecture", "XexUnloadImage" not in loader + plugin + tether)
require("build runs final polish audit", "tools/audit_final_polish.py" in build)

failed = [name for name, ok in checks if not ok]
for name, ok in checks:
    print(("PASS" if ok else "FAIL") + " | " + name)
print("FINAL POLISH AUDIT %d/%d" % (len(checks) - len(failed), len(checks)))
if failed:
    sys.exit(1)
