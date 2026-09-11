#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
checks = []

def text(path):
    return (ROOT / path).read_text(encoding='utf-8')

def check(name, condition):
    checks.append((name, bool(condition)))

platform_h = text('src/platform/xbox_platform.h')
platform_cpp = text('src/platform/xbox_platform.cpp')
loader = text('src/loader/loader_main.cpp')
app_path = text('src/shared/app_path.cpp')
boot = text('src/shared/boot_log.cpp')
early = text('src/early_trace.cpp')
plugin = text('src/plugin.cpp')
diag = text('src/diag.cpp')
usbmux = text('src/usbmux_probe.cpp')
tether = text('src/tether_driver.cpp')
xnet = text('src/native_net/xnet_bridge.cpp')
pair_crypto = text('src/pair_crypto_provider.cpp')
notify = text('src/shared/notify.cpp')
cpu_h = text('src/license/cpu_key.h')
cpu = text('src/license/cpu_key.cpp')
license_id_h = text('src/license/license_id.h')
license_id = text('src/license/license_id.cpp')
license_runtime = text('src/license/license_runtime.cpp')
licenseid_main = text('src/licenseid/licenseid_main.cpp')
tls_h = text('src/tls/bearssl_https.h')
tls = text('src/tls/bearssl_https.cpp')
build = text('build_openxechain.sh')

check('ResolveStatus type exists', 'struct ResolveStatus' in platform_h)
check('Resolve operations distinguish module/procedure calls', all(x in platform_h for x in (
    'ResolveOperationXexGetModuleHandle', 'ResolveOperationXexGetProcedureAddress',
    'ResolveOperationNullModuleHandle', 'ResolveOperationNullProcedureAddress')))
check('ResolveModuleOrdinal accepts diagnostic', 'ResolveStatus* diagnostic = 0' in platform_h)
check('XexGetModuleHandle NTSTATUS is preserved', 'diagnostic->status = status' in platform_cpp and 'XexGetModuleHandle(module_name, &module)' in platform_cpp)
check('XexGetProcedureAddress NTSTATUS is preserved', 'XexGetProcedureAddress(module, ordinal, out)' in platform_cpp)
check('ThreadStartStatus type exists', 'struct ThreadStartStatus' in platform_h)
check('ExCreateThread NTSTATUS is preserved', 'ThreadStartOperationExCreateThread' in platform_cpp and 'ExCreateThread(&thread' in platform_cpp)
check('Thread handle NtClose NTSTATUS is preserved', 'close_status = NtClose(thread)' in platform_cpp)
check('XAM write failures identify operation/result without fake status', 'operation=WriteFile result=%s status=unavailable' in platform_cpp)
check('XAM read failures identify operation/result without fake status', 'operation=ReadFile result=FALSE status=unavailable' in platform_cpp)
check('XAM close failures identify operation/result without fake status', 'operation=CloseHandle result=FALSE status=unavailable' in platform_cpp)
check('No unverified GetLastError dependency introduced', 'GetLastError' not in platform_cpp and 'GetLastError' not in platform_h)

check('Loader logs Core module lookup status', loader.count('XexGetModuleHandle(') >= 6 and 'NTSTATUS =' in loader)
check('Loader logs XexLoadImage status', 'XexLoadImage status = ' in loader)
check('Loader app-path resolver has diagnostic', 'ResolveAppDirectory(app_directory, sizeof(app_directory), &app_path_status)' in loader)
check('Boot log exposes open NTSTATUS', 'uint32_t* ntstatus_out' in text('src/shared/boot_log.h') and 'NtCreateFile failed NTSTATUS' in boot)
check('Boot log checks IO_STATUS_BLOCK on create', 'FailedStatus(io.Status)' in boot)
check('Boot log logs NtWriteFile status and IO status', 'NtWriteFile failed NTSTATUS' in boot and 'io_status=' in boot)
check('Boot log logs flush status', 'NtFlushBuffersFile failed NTSTATUS' in boot)
check('Boot log logs NtClose status', 'NtClose failed NTSTATUS' in boot)
check('Early path resolver preserves status', 'ResolveAppDirectory(gAppDirectory, sizeof(gAppDirectory), &path_diagnostic)' in early and 'resolver NTSTATUS=' in early)
check('Early native writer checks IO status', 'NativeStatusOk(io.Status)' in early)
check('Early native close captures NTSTATUS', 'const NTSTATUS status = NtClose(*file)' in early and 'NtClose failed NTSTATUS' in early)

check('Core delayed worker uses ThreadStartStatus', 'ThreadStartStatus thread_status' in plugin or 'ThreadStartStatus' in plugin)
check('Diagnostic logger worker uses ThreadStartStatus', 'ThreadStartStatus worker_status' in diag)
check('usbmux resolves each USB export with diagnostics', 'ResolveUsbExport' in usbmux and 'USB export resolve failed' in usbmux)
check('usbmux preserves USB submit/completion/open status', all(x in usbmux for x in (
    'UsbdQueueAsyncTransfer', 'USB Bulk IN completion', 'USB Bulk OUT completion',
    'UsbdOpenEndpoint(IN)', 'UsbdOpenEndpoint(OUT)', 'UsbdOpenDefaultEndpoint')))
check('usbmux cleanup logs USB NTSTATUS', all(x in usbmux for x in (
    'UsbdQueueCloseEndpoint(IN) failed NTSTATUS', 'UsbdQueueCloseDefaultEndpoint failed NTSTATUS',
    'UsbdRemoveDeviceComplete failed NTSTATUS')))
check('tether resolves each USB export with diagnostics', 'ResolveUsbExport' in tether and 'USB export resolve failed' in tether)
check('tether preserves USB submit/completion/open status', all(x in tether for x in (
    'UsbdQueueAsyncTransfer(Bulk OUT)', 'UsbdQueueAsyncTransfer(Bulk IN)',
    'USB control completion', 'UsbdOpenEndpoint(IN)', 'UsbdOpenEndpoint(OUT)',
    'UsbdOpenDefaultEndpoint')))
check('tether worker uses ThreadStartStatus', 'ThreadStartStatus diagnostic' in tether and 'ThreadStartOperationName' in tether)
check('tether cleanup logs USB NTSTATUS', all(x in tether for x in (
    'UsbdQueueCloseEndpoint(IN) NTSTATUS', 'UsbdQueueCloseDefaultEndpoint NTSTATUS',
    'UsbdRemoveDeviceComplete NTSTATUS')))

check('XNet export resolver logs ordinal and NTSTATUS', 'ResolveXamExport' in xnet and 'XAM export resolve failed' in xnet)
check('XNet callback registration logs exact result', 'XamSetEthernetInterceptCallbacks result=0x%08x' in xnet)
check('XNet link query retains returned status', 'XamGetEthernetLinkStatus status=0x%08x' in xnet)
check('Pair crypto resolver logs exact status', 'crypto export resolve failed' in pair_crypto and 'NTSTATUS=0x%08x' in pair_crypto)
check('XNotify resolver logs exact status without logger recursion', 'XNotify resolve failed' in notify and 'DbgPrint(' in notify)

check('CPU-key diagnostic type exists', 'struct CpuKeyDiagnostic' in cpu_h and 'enum CpuKeyFailure' in cpu_h)
check('CPU-key resolver status is preserved', 'CpuKeyFailureResolveExpansionCall' in cpu and '&resolve' in cpu)
check('CPU-key invalid fuse read is identified without values', 'CpuKeyFailureInvalidFuseRead' in cpu)
check('Licence-ID forwards CPU diagnostic', 'CpuKeyDiagnostic* diagnostic' in license_id_h and 'ReadCpuKey(key, diagnostic)' in license_id)
check('LicenseID app reports path resolver status', 'path resolve failed' in licenseid_main and 'NTSTATUS=0x%08x' in licenseid_main)
check('LicenseID app reports CPU access diagnostic', 'CPU key access unavailable | operation=' in licenseid_main)
check('LicenseID app labels XAM file failures status unavailable', 'CreateFileA result=NULL status=unavailable' in licenseid_main and 'WriteFile(ID) result=FALSE status=unavailable' in licenseid_main)

check('HTTPS diagnostic type exists', 'struct HttpsFetchDiagnostic' in tls_h and 'enum HttpsFetchStage' in tls_h)
check('HTTPS API accepts diagnostic', 'HttpsFetchDiagnostic* diagnostic = 0' in tls_h)
check('TLS reset captures BearSSL error', 'HttpsFetchStageTlsReset' in tls and 'br_ssl_engine_last_error(&sc.eng)' in tls)
check('TLS write/flush/read capture BearSSL error', all(stage in tls for stage in (
    'HttpsFetchStageTlsWrite', 'HttpsFetchStageTlsFlush', 'HttpsFetchStageTlsRead')) and tls.count('br_ssl_engine_last_error(&sc.eng)') >= 4)
check('HTTP non-200 preserves numeric response status', 'SetDiagnosticStatus(diagnostic, HttpsFetchStageHttpStatus, http_status)' in tls)
check('Licence runtime logs HTTPS result/stage/status', 'HTTPS check failed | result=%u (%s) stage=%s status=0x%08x' in license_runtime)
check('Licence runtime logs explicit unavailable status when none exists', 'HTTPS check failed | result=%u (%s) stage=%s status=unavailable' in license_runtime)
check('Licence runtime logs CPU resolver diagnostic', 'Local console ID unavailable | operation=%s/%s NTSTATUS=0x%08x' in license_runtime)
check('Licence worker uses ThreadStartStatus', 'ThreadStartStatus thread_diagnostic' in license_runtime)

check('All non-platform thread starts request diagnostics', all(token in source for source, token in (
    (plugin, '&worker_status'),
    (diag, '&worker_status'),
    (usbmux, '&thread_status'),
    (tether, '&diagnostic'),
    (license_runtime, '&thread_diagnostic'),
)))
check('Licence endpoint is absent from runtime/logger source', all(token not in '\n'.join((license_runtime, diag, notify, plugin, tether, xnet, licenseid_main)) for token in (
    'raw.githubusercontent.com', 'rghmodder1991', 'License360')))
check('HTTPS transport source contains no diagnostic logger call', 'it360_diag::' not in tls and 'DbgPrint(' not in tls)
check('Step 3 audit is wired into build', 'tools/audit_step3.py' in build)

passed = sum(1 for _, ok in checks if ok)
for name, ok in checks:
    print(('PASS' if ok else 'FAIL') + ' | ' + name)
print('STEP3 AUDIT %d/%d' % (passed, len(checks)))
if passed != len(checks):
    sys.exit(1)
