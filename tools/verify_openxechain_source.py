#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]

def read(path):
    return (root / path).read_text()

def sec(text, start, end=None):
    i = text.find(start)
    if i < 0:
        return ''
    if end is None:
        return text[i:]
    j = text.find(end, i + len(start))
    return text[i:j if j >= 0 else len(text)]

checks = []

def want(condition, message):
    checks.append((bool(condition), message))

platform_h = read('src/platform/xbox_platform.h')
platform = read('src/platform/xbox_platform.cpp')
entry = read('src/entry.cpp')
early = read('src/early_trace.cpp')
plugin = read('src/plugin.cpp')
transport = read('src/usbmux_probe.cpp')
tether = read('src/tether_driver.cpp')
ncm = read('src/ipheth_ncm.cpp')
net = read('src/net_stack.cpp')
net_h = read('src/net_stack.h')
proto = read('src/pair_protocol.cpp')
session = read('src/pair_session.cpp')
crypto = read('src/pair_crypto_provider.cpp')
identity = read('src/pair_identity_generated.h')
store = read('src/pair_store.cpp')
diag = read('src/diag.cpp')
notify = read('src/shared/notify.cpp')
boot_log = read('src/shared/boot_log.cpp')
hook = read('src/hook17559.cpp')
build = read('build_openxechain.sh')
workflow = read('.github/workflows/build-xex.yml')
launch = read('launch.ini.example')

# OpenXeChain ABI / build migration.
want(
    '#include <xecore/xboxkrnl.h>' in platform_h and
    '#include <xecore/xam.h>' in platform_h,
    'platform layer uses xecorelib headers'
)

want(
    'ExCreateThread' in platform and
    'NtClose' in platform and
    'RawThreadThunk' in platform and
    'ExTerminateThread' in platform,
    'raw kernel thread creation has explicit OpenXeChain-safe termination thunk'
)

want(
    'KeDelayExecutionThread' in platform,
    'kernel relative delay replaces sleep wrapper'
)

want(
    '__sync_val_compare_and_swap' in platform and
    '__sync_lock_test_and_set' in platform,
    'PowerPC compiler atomics replace legacy atomic wrappers'
)

want(
    'XexGetModuleHandle' in platform and
    'XexGetProcedureAddress' in platform,
    'module ordinal resolution centralized'
)

want(
    'ResolveModuleOrdinal("xboxkrnl.exe", 344' in platform,
    'kernel build resolved through xboxkrnl data export'
)

want(
    'extern "C" int DllMain(unsigned int module_handle, unsigned int reason, unsigned int)' in plugin,
    'DllMain signature matches OpenXeChain ABI'
)

want(
    'KernelBuild(&kernel_status)' in plugin and
    'build != 17559' in plugin,
    '17559 fail-closed kernel gate retained'
)

want(
    'extern "C" void _start(' in entry and
    'crtinit();' in entry and
    '__CTOR_LIST__' in entry and
    'DllMain(Handle, Reason, Reserved)' in entry,
    'project diagnostic entrypoint reproduces OpenXeChain CRT/constructor/DllMain sequence'
)

want(
    'BeginSessionLiteral(' in entry and
    'ENTRY 00:' in entry and
    'ENTRY CTOR BEFORE' in entry and
    'ENTRY 05:' in entry,
    'pre-CRT and constructor boundaries are persistently instrumented'
)

want(
    r'\\Device\\Harddisk0\\Partition1\\iPhoneTether360.log' in early and
    'NtCreateFile' in early and
    'NtWriteFile' in early and
    'NtFlushBuffersFile' in early and
    'NtClose' in early,
    'ultra-early logger writes directly to the internal HDD through xboxkrnl'
)

want(
    'FILE_APPEND_DATA | SYNCHRONIZE' in early and
    'FILE_OPEN_IF' in early and
    'FILE_SYNCHRONOUS_IO_NONALERT' in early,
    'native trace uses append-only synchronous file semantics'
)

want(
    'IT360_ALIGN8' in identity and
    '__declspec' not in identity,
    'embedded XeCrypt keys use clang alignment attribute'
)

want(
    'static const DWORD gMatcherTrampoline[13]' in hook and
    'memset(gMatcherTrampoline' not in hook,
    'executable trampoline is immutable after XEX mapping'
)

want(
    'FlushInstructionCache(src, sizeof(patch))' in hook and
    'dcbst 0,%0' in platform and
    'icbi 0,%0' in platform,
    '17559 kernel patch explicitly synchronizes Xenon data/instruction caches'
)

want(
    'HOOK 03A:' in hook and
    'HOOK 06A:' in hook and
    'HOOK 08A:' in hook,
    'matcher patch records live prologue, trampoline and post-patch state'
)

want(
    '--target=ppc32-xbox360' in build and
    '/subsystem:xbox360' in build,
    'OpenXeChain Xbox target/subsystem selected'
)

want(
    '/entry:_start' in build and
    '--type sysdll' in build,
    'custom diagnostic entry and SynthXEX sysdll packaging selected'
)

want(
    'src/entry.cpp' in build and
    'src/early_trace.cpp' in build,
    'custom entry and native HDD trace are compiled into the XEX'
)

want(
    '/base:0x91DE0000' in build and
    '/align:4096' in build,
    'plugin image base and SynthXEX-valid alignment fixed'
)

want(
    'verify_openxechain_pe.py' in build and
    'verify_openxechain_xex.py' in build,
    'PE and final XEX are structurally verified'
)

want(
    'OXC_BUILDSCRIPT_COMMIT: eed1fa65bf9577fd31625764b320a90182ea9ade' in workflow,
    'GitHub build pins current OpenXeChain aggregate revision'
)

want(
    'actions/upload-artifact@v4' in workflow and
    'build/Loader.xex' in workflow and
    'build/Core.xex' in workflow and
    'build/LicenseID.xex' in workflow and
    'iPhoneTether360-B9C-OXC.xex' not in workflow,
    'GitHub Actions publishes the three final XEX artifacts'
)

# Pair/Trust retained.
want(
    'XmlExtractDataB64Value(xml, "Value"' in session,
    'DevicePublicKey parsed from lockdownd Value'
)

want(
    'XmlExtractStringValue(xml, "Value"' in session,
    'string GetValue parsed from lockdownd Value'
)

pair_build = sec(
    proto,
    'size_t BuildPairingRequestXml',
    'size_t BuildPairingFrame'
)

want(
    'RootPrivateKey' not in pair_build and
    'HostPrivateKey' not in pair_build,
    'Pair request never transmits host private keys'
)

want(
    'ExtendedPairingErrors' in proto and
    'PairingOptions' in proto,
    'extended pairing options retained'
)

want(
    'PairingDialogResponsePending' in proto and
    'StartTrustRetry' in transport,
    'iOS Trust-pending retry retained'
)

want(
    'LoadPairRecord' in transport and
    'BuildValidatePairFrame' in transport,
    'persistent ValidatePair reconnect retained'
)

want(
    'EscrowBag' in transport and
    'SavePairRecord' in transport,
    'EscrowBag persistence retained'
)

want(
    'DeletePairRecord' in transport,
    'stale pairing recovery retained'
)

want(
    'ResolveOrdinal(364' in crypto and
    'ResolveOrdinal(402' in crypto,
    'XeCrypt RSA/SHA1 ordinals retained'
)

want(
    'BuildDeviceCertificatePem' in crypto and
    'SignRootSha256' in crypto,
    'runtime device certificate creation retained'
)

want(
    'OpenTruncate' in store and
    'OpenRead' in store and
    'RemoveFile' in store,
    'pair store routes filesystem calls through OpenXeChain platform layer'
)

# Apple USB tether / network stack retained.
want('kAppleVendor = 0x05AC' in tether, 'Apple VID exact')

want(
    'kInterfaceNumber = 2' in tether and
    'kAltSetting = 1' in tether,
    'ipheth interface 2 alt 1'
)

want(
    'kClass = 0xFF' in tether and
    'kSubclass = 0xFD' in tether and
    'kProtocol = 0x01' in tether,
    'FF/FD/01 tether match'
)

want(
    'QueueControl(CtrlGetMac, 0xC0, 0x00, 0, 2, kCtrlSize)' in tether,
    'Apple GET_MACADDR control'
)

want(
    'QueueControl(CtrlEnableNcm, 0x41, 0x04, 0, 2, 0)' in tether,
    'Apple ENABLE_NCM control'
)

want(
    'QueueControl(CtrlCarrier, 0xC0, 0x45, 0, 2, kCtrlSize)' in tether,
    'Apple CARRIER_CHECK control'
)

want(
    'QueueControl(CtrlSetInterface, 0x01, 0x0B, kAltSetting, kInterfaceNumber, 0)' in tether,
    'SET_INTERFACE(2,1) retained'
)

want(
    'IsPairReady()' in tether and
    'TetherWaitingPair' in tether,
    'tether activation waits for Pair/ValidatePair'
)

want(
    'return it360_tether::Driver()' in transport,
    'dynamic USB matcher routes tether interface'
)

want('kRxSize = 65536' in tether, '64 KiB NCM RX buffer')

want(
    '0x484D434E' in ncm and
    '0x304D434E' in ncm and
    'kMaxDpe = 22' in ncm,
    'Apple NCM parser retained'
)

want(
    'LegacyFrameLength' in ncm and
    'data + 2' in ncm,
    'legacy ipheth RX fallback retained'
)

worker = sec(
    tether,
    'static uint32_t Worker(void* p) {',
    'static bool StartWorker() {'
)

consume = sec(
    tether,
    'static bool ConsumeEthernet',
    'static bool PopEthernet'
)

want(
    'gNet.OnEthernetFrame' in worker and
    'gNet.TickOneSecond' in worker,
    'single worker owns network state'
)

want(
    'gNet.OnEthernetFrame' not in consume and
    'rxQ' in consume,
    'USB callback only queues Ethernet frames'
)

want(
    'SendDhcpDiscover' in net and
    'SendDhcpRequest' in net and
    'SendDhcpRenew' in net,
    'DHCP/renewal retained'
)

want(
    'SendArpRequest' in net and
    'SendPing' in net and
    'HandleIcmp' in net,
    'ARP/ICMP retained'
)

want(
    'SendDnsQuery' in net and
    'SkipDnsName' in net,
    'DNS resolver retained'
)

want(
    'SendTcpSyn' in net and
    'HandleTcp' in net and
    'GET / HTTP/1.0' in net,
    'TCP/HTTP validation retained'
)

want(
    'InternetValidated()' in net_h,
    'Internet validation state exposed'
)

want(
    'SUCCESS standalone Internet HTTP response received through iPhone' in tether,
    'final standalone Internet success marker retained'
)

# Diagnostics.
want(
    r'\\Device\\Harddisk0\\Partition1\\iPhoneTether360.log' in early and
    'NtCreateFile' in early and 'NtWriteFile' in early and
    'NtFlushBuffersFile' in early and 'NtClose' in early,
    'pre-CRT emergency logger is internal-HDD native I/O only'
)

want(
    'Boot.log' in early and
    'RuntimeLogPath' in diag and
    'Log.txt' in early and
    'gNormalReady' in early and
    'ReplayEmergencyIntoNormalUnlocked' in early,
    'normal diagnostics switch to app-local Boot.log and Log.txt'
)

want(
    'kQueueDepth = 256' in diag and
    'gDroppedLogs' in diag and
    'StartDetachedThread' in diag and
    'Worker' in diag,
    'runtime logger remains asynchronous with enlarged queue and drop accounting'
)

want(
    'PhoneNotice' in diag and 'QueueNotification' in diag and
    'ResolveModuleOrdinal("xam.xex", 656u' in notify and
    'KeGetCurrentProcessType()' in notify and
    'ResolveModuleOrdinal("xam.xex", 1084u' in notify and
    'UserNotifyThread' in notify,
    'XNotify is queued and marshalled to XAM user-thread context from system workers'
)

want(
    'Address32(' in transport and
    'Address32(' in tether,
    'USB callback addresses are explicitly narrowed for 32-bit target'
)

usb_h = read('src/xbox17559_usb.h')

want(
    'usb_trb_28' in usb_h and
    'usb_control_trb_40' in usb_h,
    'OpenXeChain target build asserts recovered 17559 USB ABI layouts'
)

want(
    'Core.xex' in launch and 'iPhoneTether360-B9C-OXC.xex' not in launch,
    'deployment example names final resident Core.xex'
)

failed = [
    message
    for ok, message in checks
    if not ok
]

for ok, message in checks:
    print(
        ('PASS ' if ok else 'FAIL ') +
        message
    )

if failed:
    print(
        'OpenXeChain source verifier: FAIL (%d/%d)' %
        (len(failed), len(checks)),
        file=sys.stderr
    )
    sys.exit(1)

print(
    'OpenXeChain source verifier: PASS (%d checks)' %
    len(checks)
)
