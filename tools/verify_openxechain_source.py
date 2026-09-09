#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
def read(p): return (root / p).read_text()
def sec(t, a, b=None):
    i=t.find(a)
    if i<0: return ''
    if b is None: return t[i:]
    j=t.find(b,i+len(a)); return t[i:j if j>=0 else len(t)]

checks=[]
def want(c,m): checks.append((bool(c),m))

platform_h=read('src/platform/xbox_platform.h')
platform=read('src/platform/xbox_platform.cpp')
plugin=read('src/plugin.cpp')
transport=read('src/usbmux_probe.cpp')
tether=read('src/tether_driver.cpp')
ncm=read('src/ipheth_ncm.cpp')
net=read('src/net_stack.cpp')
net_h=read('src/net_stack.h')
proto=read('src/pair_protocol.cpp')
session=read('src/pair_session.cpp')
crypto=read('src/pair_crypto_provider.cpp')
identity=read('src/pair_identity_generated.h')
store=read('src/pair_store.cpp')
diag=read('src/diag.cpp')
hook=read('src/hook17559.cpp')
build=read('build_openxechain.sh')
workflow=read('.github/workflows/build-xex.yml')
launch=read('launch.ini.example')

# OpenXeChain ABI / build migration.
want('#include <xecore/xboxkrnl.h>' in platform_h and '#include <xecore/xam.h>' in platform_h,
     'platform layer uses xecorelib headers')
want('ExCreateThread' in platform and 'NtClose' in platform and 'RawThreadThunk' in platform and 'ExTerminateThread' in platform,
     'raw kernel thread creation has explicit OpenXeChain-safe termination thunk')
want('KeDelayExecutionThread' in platform, 'kernel relative delay replaces sleep wrapper')
want('__sync_val_compare_and_swap' in platform and '__sync_lock_test_and_set' in platform,
     'PowerPC compiler atomics replace legacy atomic wrappers')
want('XexGetModuleHandle' in platform and 'XexGetProcedureAddress' in platform,
     'module ordinal resolution centralized')
want('ResolveModuleOrdinal("xboxkrnl.exe", 344' in platform, 'kernel build resolved through xboxkrnl data export')
want('extern "C" int DllMain(unsigned int module_handle, unsigned int reason, unsigned int)' in plugin,
     'DllMain signature matches OpenXeChain newlib CRT')
want('KernelBuild()' in plugin and 'build != 17559' in plugin, '17559 fail-closed kernel gate retained')
want('IT360_ALIGN8' in identity and '__declspec' not in identity, 'embedded XeCrypt keys use clang alignment attribute')
want('static const DWORD gMatcherTrampoline[13]' in hook and 'memset(gMatcherTrampoline' not in hook,
     'executable trampoline is immutable after XEX mapping')
want('FlushInstructionCache(src, sizeof(patch))' in hook and 'dcbst 0,%0' in platform and 'icbi 0,%0' in platform,
     '17559 kernel patch explicitly synchronizes Xenon data/instruction caches')
want('--target=ppc32-xbox360' in build and '/subsystem:xbox360' in build, 'OpenXeChain Xbox target/subsystem selected')
want('/entry:_start' in build and '--type sysdll' in build, 'OpenXeChain CRT entry and SynthXEX sysdll packaging selected')
want('/base:0x91DE0000' in build and '/align:4096' in build, 'plugin image base and SynthXEX-valid alignment fixed')
want('verify_openxechain_pe.py' in build and 'verify_openxechain_xex.py' in build,
     'PE and final XEX are structurally verified')
want('OXC_BUILDSCRIPT_COMMIT: eed1fa65bf9577fd31625764b320a90182ea9ade' in workflow,
     'GitHub build pins current OpenXeChain aggregate revision')
want('actions/upload-artifact@v4' in workflow and 'iPhoneTether360-B9C-OXC.xex' in workflow,
     'GitHub Actions publishes finished XEX')

# Pair/Trust retained.
want('XmlExtractDataB64Value(xml, "Value"' in session, 'DevicePublicKey parsed from lockdownd Value')
want('XmlExtractStringValue(xml, "Value"' in session, 'string GetValue parsed from lockdownd Value')
pair_build=sec(proto,'size_t BuildPairingRequestXml','size_t BuildPairingFrame')
want('RootPrivateKey' not in pair_build and 'HostPrivateKey' not in pair_build,
     'Pair request never transmits host private keys')
want('ExtendedPairingErrors' in proto and 'PairingOptions' in proto, 'extended pairing options retained')
want('PairingDialogResponsePending' in proto and 'StartTrustRetry' in transport, 'iOS Trust-pending retry retained')
want('LoadPairRecord' in transport and 'BuildValidatePairFrame' in transport, 'persistent ValidatePair reconnect retained')
want('EscrowBag' in transport and 'SavePairRecord' in transport, 'EscrowBag persistence retained')
want('DeletePairRecord' in transport, 'stale pairing recovery retained')
want('ResolveOrdinal(364' in crypto and 'ResolveOrdinal(402' in crypto, 'XeCrypt RSA/SHA1 ordinals retained')
want('BuildDeviceCertificatePem' in crypto and 'SignRootSha256' in crypto, 'runtime device certificate creation retained')
want('OpenTruncate' in store and 'OpenRead' in store and 'RemoveFile' in store,
     'pair store routes filesystem calls through OpenXeChain platform layer')

# Apple USB tether / network stack retained.
want('kAppleVendor = 0x05AC' in tether, 'Apple VID exact')
want('kInterfaceNumber = 2' in tether and 'kAltSetting = 1' in tether, 'ipheth interface 2 alt 1')
want('kClass = 0xFF' in tether and 'kSubclass = 0xFD' in tether and 'kProtocol = 0x01' in tether,
     'FF/FD/01 tether match')
want('QueueControl(CtrlGetMac,0xC0,0x00,0,2,kCtrlSize)' in tether, 'Apple GET_MACADDR control')
want('QueueControl(CtrlEnableNcm,0x41,0x04,0,2,0)' in tether, 'Apple ENABLE_NCM control')
want('QueueControl(CtrlCarrier,0xC0,0x45,0,2,kCtrlSize)' in tether, 'Apple CARRIER_CHECK control')
want('QueueControl(CtrlSetInterface,0x01,0x0B,kAltSetting,kInterfaceNumber,0)' in tether,
     'SET_INTERFACE(2,1) retained')
want('IsPairReady()' in tether and 'TetherWaitingPair' in tether, 'tether activation waits for Pair/ValidatePair')
want('return it360_tether::Driver()' in transport, 'dynamic USB matcher routes tether interface')
want('kRxSize = 65536' in tether, '64 KiB NCM RX buffer')
want('0x484D434E' in ncm and '0x304D434E' in ncm and 'kMaxDpe = 22' in ncm, 'Apple NCM parser retained')
want('LegacyFrameLength' in ncm and 'data + 2' in ncm, 'legacy ipheth RX fallback retained')
worker=sec(tether,'static uint32_t Worker(void* p) {','static bool StartWorker() {')
consume=sec(tether,'static bool ConsumeEthernet','static bool PopEthernet')
want('gNet.OnEthernetFrame' in worker and 'gNet.TickOneSecond' in worker, 'single worker owns network state')
want('gNet.OnEthernetFrame' not in consume and 'rxQ' in consume, 'USB callback only queues Ethernet frames')
want('SendDhcpDiscover' in net and 'SendDhcpRequest' in net and 'SendDhcpRenew' in net, 'DHCP/renewal retained')
want('SendArpRequest' in net and 'SendPing' in net and 'HandleIcmp' in net, 'ARP/ICMP retained')
want('SendDnsQuery' in net and 'SkipDnsName' in net, 'DNS resolver retained')
want('SendTcpSyn' in net and 'HandleTcp' in net and 'GET / HTTP/1.0' in net, 'TCP/HTTP validation retained')
want('InternetValidated()' in net_h, 'Internet validation state exposed')
want('SUCCESS standalone Internet HTTP response received through iPhone' in tether, 'final standalone Internet success marker retained')

# Diagnostics and safe dynamic imports.
want('OpenAppend(paths[i])' in diag and r'Hdd1:\\iPhoneTether360.log' in diag and r'Usb3:\\iPhoneTether360.log' in diag,
     'persistent logging uses platform I/O with HDD/USB fallback')
want('StartDetachedThread(Worker, 0)' in diag and 'kQueueDepth' in diag, 'diagnostic file I/O remains asynchronous')
want('#if defined(IT360_XBOX) && defined(IT360_ENABLE_XNOTIFY)' in diag and 'ResolveModuleOrdinal("xam.xex", 0x290' in diag, 'XNotify is compile-time gated off by default for system-thread safety')
want('Address32(' in transport and 'Address32(' in tether, 'USB callback addresses are explicitly narrowed for 32-bit target')
want('usb_trb_28' in read('src/xbox17559_usb.h') and 'usb_control_trb_40' in read('src/xbox17559_usb.h'),
     'OpenXeChain target build asserts recovered 17559 USB ABI layouts')
want('iPhoneTether360-B9C-OXC.xex' in launch, 'deployment example names OpenXeChain XEX')

failed=[m for ok,m in checks if not ok]
for ok,m in checks: print(('PASS ' if ok else 'FAIL ')+m)
if failed:
    print('OpenXeChain source verifier: FAIL (%d/%d)'%(len(failed),len(checks)),file=sys.stderr)
    sys.exit(1)
print('OpenXeChain source verifier: PASS (%d checks)'%len(checks))
