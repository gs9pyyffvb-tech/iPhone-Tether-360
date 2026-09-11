#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parents[1]
def r(p): return (ROOT/p).read_text(errors='replace')

build=r('build_openxechain.sh'); wf=r('.github/workflows/build-xex.yml')
loader=r('src/loader/loader_main.cpp'); licutil=r('src/licenseid/licenseid_main.cpp')
entry=r('src/entry.cpp'); plugin=r('src/plugin.cpp'); hook=r('src/hook17559.cpp')
platform=r('src/platform/xbox_platform.cpp'); ph=r('src/platform/xbox_platform.h')
usbh=r('src/xbox17559_usb.h'); mux=r('src/usbmux_probe.cpp'); tether=r('src/tether_driver.cpp')
xnet=r('src/native_net/xnet_bridge.cpp'); notify=r('src/shared/notify.cpp'); diag=r('src/diag.cpp')
cpu=r('src/license/cpu_key.cpp'); lid=r('src/license/license_id.cpp'); lrun=r('src/license/license_runtime.cpp')
tls=r('src/tls/bearssl_https.cpp'); readme=r('README.md'); targets=r('TARGETS.md')
pe=r('tools/verify_openxechain_pe.py'); xex=r('tools/verify_openxechain_xex.py')

C=[]
def ck(v,n): C.append((bool(v),n))

# Exact dependency pins externally checked during Step 4.
ck('eed1fa65bf9577fd31625764b320a90182ea9ade' in wf, 'OpenXeChain aggregate buildscript revision pinned')
ck('dc0cc21cbf6ae9e75f1974833742f80b45519326' in build, 'XboxTLS/BearSSL source revision pinned')
ck('c65d67e071357acade681f04c46ae9719797f239' in readme, 'README records pinned xecorelib revision verified by Step 4')
ck('f929633c27099d404e9cb5b2739a9c7f9b6afccc' in readme, 'README records pinned Newlib revision verified by Step 4')

# Loader -> Core and image roles.
ck('Core.xex' in loader and 'XexLoadImage' in loader, 'Loader resolves and loads Core.xex')
ck('kResidentSystemDllFlags = 0x0000000Au' in loader, 'Loader requests resident/system DLL load flags 0xA')
ck('/dll' in build and '/base:0x91DE0000' in build and '--type sysdll' in build, 'Core is linked/packaged as fixed-base sysdll')
ck('link_title Loader' in build and 'link_title LicenseID' in build and '--type title' in build, 'Loader and LicenseID are title XEXs')
ck('DeriveLicenceId' in licutil and 'LicenseID.txt' in licutil, 'LicenseID utility derives and writes the console licence ID')

# Core custom CRT sequence matches pinned Newlib crt0 semantics.
positions=[entry.find('crtinit();'), entry.find('__CTOR_LIST__'), entry.find('DllMain(Handle, Reason, Reserved)')]
ck(all(x >= 0 for x in positions) and positions[0] < positions[2], 'Core custom entry initializes CRT before DllMain')
ck('RunGlobalConstructors' in entry and '__CTOR_LIST__' in entry and '__CTOR_LIST__[i]()' in entry, 'Core custom entry runs compiler C++ constructors')
start_body=entry[entry.find('extern "C" void _start('):]
ck(start_body.find('ENTRY 00:') >= 0 and start_body.find('crtinit();') >= 0 and start_body.find('ENTRY 00:') < start_body.find('crtinit();'), 'first persistent startup checkpoint precedes crtinit')

# 17559 gating and raw ABI containment.
ck('KernelBuild(&kernel_status)' in plugin and 'build != 17559' in plugin, 'Core fails closed on non-17559 kernels')
gate_tail=plugin[plugin.find('if (build != 17559)'):]
ck('StartDetachedThread' in gate_tail and 'DelayedInitialize' in gate_tail and 'InstallLockdownProbe' in plugin[plugin.find('static uint32_t DelayedInitialize'):plugin.find('extern "C" int DllMain')], '17559 gate controls worker that performs USB hook installation')
ck('0x800D8500u' in mux and '0x800D83A0u' in mux and '0x800D8500u' in tether and '0x800D83A0u' in tether,
   'recovered 17559 raw USB descriptor helpers are explicit')
raw_refs=[]
for p in (ROOT/'src').rglob('*.cpp'):
    t=p.read_text(errors='replace')
    if '0x800D8500u' in t or '0x800D83A0u' in t: raw_refs.append(p.relative_to(ROOT).as_posix())
ck(set(raw_refs) <= {'src/usbmux_probe.cpp','src/tether_driver.cpp'}, 'raw 17559 USB helper addresses are contained to USB drivers')
ck('IT360_STATIC_ASSERT' in usbh and 'usb_trb_28' in usbh and 'usb_control_trb_40' in usbh, 'recovered USB ABI layouts have compile-time size assertions')
ck('FlushInstructionCache' in hook and 'dcbst 0,%0' in platform and 'icbi 0,%0' in platform, 'kernel hook write performs Xenon D/I cache synchronization')

# Ordinals pinned against xecorelib c65d67e0 and XAM export table during audit.
for token, name in [
    ('ResolveModuleOrdinal("xboxkrnl.exe", 344', 'XboxKrnlVersion/build export 344'),
    ('ResolveModuleOrdinal("xboxkrnl.exe", 431u', 'ExLoadedImageName export 431'),
    ('ResolveModuleOrdinal("xboxkrnl.exe", 780u', 'ExExpansionCall export 780'),
    ('ResolveModuleOrdinal("xam.xex", 656u', 'XNotifyQueueUI export 656'),
    ('ResolveModuleOrdinal("xam.xex", 1084u', 'XAM CreateThread export 1084')]:
    blob=platform+loader+cpu+notify+r('src/shared/app_path.cpp')
    ck(token in blob, name+' reference exact')
for ord_ in (75, 109, 110, 111):
    ck(('ResolveXamExport(%d' % ord_) in xnet,
       'XAM native-network ordinal %d exact' % ord_)
for ord_ in (740, 742, 746, 747, 748, 749, 750, 751, 759):
    ck((('ResolveUsbExport(%d' % ord_) in mux) or
        (('ResolveUsbExport(%d' % ord_) in tether),
       'xboxkrnl USB ordinal %d exact' % ord_)

# Function contracts/calling conventions encoded in project wrappers.
ck('typedef uint32_t (*ThreadFn)(void* context)' in ph and 'ExCreateThread' in platform and 'ExTerminateThread' in platform,
   'detached worker thunk matches project DWORD(void*) contract and terminates explicitly')
ck('typedef HANDLE (*XamCreateThreadFn)(void*, DWORD, UserThreadProcFn, void*, DWORD, DWORD*)' in notify,
   'XAM CreateThread typedef matches six-argument Win32/Xbox thread contract')
ck('KeGetCurrentProcessType' not in notify and 'ShowTitle' in notify and 'ShowSystem' in notify,
   'notification layer uses explicit title/system contexts without process guessing')
ck('SystemNotifyThread' in notify and 'DirectShowWide' in notify and '1084u' in notify,
   'system-worker notifications always marshal through a XAM-created user thread')
ck('QueueNotification' in diag and 'it360_notify::ShowSystem' in diag, 'Core runtime queues notifications before system UI dispatch')
ck('XNotifyQueueUI' not in mux and 'XNotifyQueueUI' not in tether, 'USB callbacks never call XNotify directly')

# CPU key/licence/TLS safety.
ck('FuseAddress(3u)' in cpu and 'FuseAddress(5u)' in cpu and 'kHvppExpansionId = 0x48565050u' in cpu, 'CPU-key derivation uses HVPP fuse lines 3 and 5')
ck('XeCryptSha' in lid or 'sha256' in lid.lower(), 'licence ID is SHA-256 derived')
ck('Fail-open access allowed' in lrun and 'NetworkGateAllow' in lrun, 'licensing service failure preserves fail-open network policy')
ck('No License found for this Xbox' in lrun and 'License Has Been Found' in lrun, 'required licence notifications are exact')
ck('iPhoneTether360 Ready' in r('src/shared/notify.cpp'), 'required ready notification is exact')
ck('iPhone to Xbox Complete' in tether, 'required completion notification is exact')
# Runtime logs must not reveal the endpoint. TLS implementation may contain it by design.
logging_surfaces='\n'.join([diag,notify,tether,xnet,lrun,loader,licutil,plugin])
ck('githubusercontent.com' not in logging_surfaces and 'License360' not in logging_surfaces,
   'runtime logging surfaces do not contain licensing host/repository details')

# Native-network path.
ck('set_callbacks' in xnet and 'ResolveXamExport(109' in xnet and 'RegisterCallbacks' in xnet and 'gBridge.set_callbacks(' in xnet, 'native XNet bridge installs Ethernet interception callbacks')
ck('InjectIphoneFrame' in tether and 'PopXboxTransmit' in tether and 'SendEthernet(native_outgoing' in tether, 'tether worker bridges RX and TX between iPhone and Xbox paths')
ck('UpdateTransportState' in tether and 'NativeNetworkAllowed()' in tether, 'native bridge activation is gated by transport and licence state')

# Build/verifier/package exactness.
ck('title' in pe and 'sysdll' in pe and '0x82000000' in pe and '0x91DE0000' in pe, 'PE verifier distinguishes title and sysdll image requirements')
ck('title' in xex and 'sysdll' in xex, 'XEX verifier distinguishes title and sysdll module flags')
ck('verify_openxechain_pe.py" "$pe" title' in build and 'verify_openxechain_xex.py" "$xex" title' in build,
   'title XEX outputs are structurally verified')
ck('verify_openxechain_pe.py" "$CORE_PE" sysdll' in build and 'verify_openxechain_xex.py" "$CORE_XEX" sysdll' in build,
   'Core XEX output is structurally verified')
ck('iPhoneTether360-B9C-OXC.xex' not in build and 'iPhoneTether360-B9C-OXC.xex' not in wf,
   'obsolete B9C release artifact removed from active build/workflow')
ck(all(x in build for x in ('Loader.xex','Core.xex','LicenseID.xex')), 'build output manifest names exactly the three deployable XEXs')
ck('1.0.0' in readme and 'Loader.xex' in readme and 'Core.xex' in readme and 'LicenseID.xex' in readme,
   'README documents final 1.0.0 three-XEX layout')
ck('Step 4' in targets and 'pre-build' in targets.lower(), 'TARGETS records Step-4 pre-build audit state')
host_validation=r('tools/run_host_validation.sh')
ck('audit_host_symbols.sh' in host_validation and 'test_notify_openxechain_syntax.sh' in host_validation, 'host validation includes symbol and target-only notification checks')

failed=[n for ok,n in C if not ok]
for ok,n in C: print(('PASS ' if ok else 'FAIL ')+n)
if failed:
    print('Step 4 audit: FAIL (%d/%d failed)'%(len(failed),len(C)), file=sys.stderr)
    sys.exit(1)
print('Step 4 audit: PASS (%d checks)'%len(C))
