#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
checks = []

def text(path):
    p = ROOT / path
    if not p.exists():
        return ""
    return p.read_text(errors="replace")

def require(name, condition):
    checks.append((name, bool(condition)))

bridge = text("src/native_net/xnet_bridge.cpp")
bridge_h = text("src/native_net/xnet_bridge.h")
translate = text("src/native_net/frame_translate.cpp")
tether = text("src/tether_driver.cpp")
build = text("build_openxechain.sh")
targets = text("TARGETS.md")

require("bridge source exists", bool(bridge))
require("bridge header exists", bool(bridge_h))
require("frame translator exists", bool(translate))
require("XAM ordinal 75 link status resolved", 'ResolveXamExport(75' in bridge)
require("XAM ordinal 109 resolved", 'ResolveXamExport(109' in bridge)
require("XAM ordinal 110 resolved", 'ResolveXamExport(110' in bridge)
require("XAM ordinal 111 resolved", 'ResolveXamExport(111' in bridge)
require("system-app caller class", "kXnCallerSysApp = 2u" in bridge)
require("callbacks registered", "gBridge.set_callbacks" in bridge and "&XboxTransmitCallback" in bridge)
require("base interception flags zero", "&gBridge,\n                                             0u" in bridge)
require("incoming frames injected with export 111", "gBridge.intercept_recv(kXnCallerSysApp" in bridge)
require("intercept xmit export retained", "InterceptXmitFn" in bridge and "gBridge.intercept_xmit" in bridge)
require("native transmit bounded queue", "kQueueDepth = 64u" in bridge and "FrameSlot queue[kQueueDepth]" in bridge)
require("frame size bounded", "kFrameMax = 1514u" in bridge)
require("callback lock is bounded", "TryLock(&gBridge.queue_lock, 64u)" in bridge)
require("registration deferred", "UpdateTransportState" in bridge and "should_enable" in bridge)
require("phone detach pauses bridge", "PhoneDetached" in bridge and "FlushQueue" in bridge)
require("no guessed link option 1395", "1395" not in bridge and "1395" not in tether)
require("no kernel patch in B9D module", "FlushInstructionCache" not in bridge and "memcpy((void*)0x" not in bridge)

# Packet callbacks must stay timing-safe: no diagnostics, filesystem, sleeps,
# USB calls, allocation, or receive injection from inside callback bodies.
def body(fn):
    marker = "static int " + fn
    start = bridge.find(marker)
    if start < 0:
        return ""
    brace = bridge.find("{", start)
    if brace < 0:
        return ""
    depth = 0
    for i in range(brace, len(bridge)):
        if bridge[i] == "{": depth += 1
        elif bridge[i] == "}":
            depth -= 1
            if depth == 0:
                return bridge[brace:i+1]
    return ""

for fn in ("XboxTransmitCallback", "XboxReceiveCallback"):
    b = body(fn)
    require(fn + " exists", bool(b))
    for banned in ("it360_diag::", "Notify(", "SleepMs(", "queueAsyncTransfer", "CreateFile", "malloc(", "new ", "intercept_recv("):
        require(fn + " avoids " + banned, banned not in b)
    require(fn + " avoids frame translation", "TranslateOutboundFrame" not in b and "TranslateInboundFrame" not in b)

require("Ethernet source MAC translated", "frame + 6" in translate and "native_mac, tether_mac" in translate)
require("Ethernet destination MAC translated", "ReplaceMacIfEqual(frame, tether_mac, native_mac)" in translate)
require("ARP sender translated", "arp + 8" in translate)
require("ARP target translated", "arp + 18" in translate)
require("BOOTP chaddr translated", "bootp + 28" in translate)
require("DHCP client-id option translated", "code == 61u" in translate)
require("UDP checksum recomputed", "UdpChecksum" in translate and "Write16(udp + 6" in translate)
require("fragmented DHCP rejected", "frag & 0x3FFFu" in translate)
require("VLAN parsing supported", "0x8100u" in translate and "0x88A8u" in translate)

require("tether includes B9D bridge", '#include "native_net/xnet_bridge.h"' in tether)
require("B9D initialized with driver", "InitializeBridge()" in tether)
require("licence gate controls native activation", "it360_license_runtime::NativeNetworkAllowed()" in tether)
require("transport readiness controls activation", "gCtx.carrierOn && gCtx.netStarted && gNet.IsConfigured()" in tether)
require("iPhone RX injected into XNet", "InjectIphoneFrame(incoming.data, incoming.len)" in tether)
require("native TX drained on tether worker", "PopXboxTransmit(native_outgoing" in tether)
require("native TX enters USB queue", "SendEthernet(native_outgoing, native_length, 0)" in tether)
require("bridge paused on phone remove", "it360_native_net::PhoneDetached();" in tether)
require("B9D diagnostics run on worker", "ReportDiagnostics()" in tether)
require("private stack consumes before native inject", tether.find("OnEthernetFrame(incoming.data") < tether.find("InjectIphoneFrame(incoming.data"))
require("build includes frame translator", "src/native_net/frame_translate.cpp" in build)
require("build includes XNet bridge", "src/native_net/xnet_bridge.cpp" in build)
require("build invokes step17 audit", "tools/audit_step17.py" in build)
require("build runs frame translation test", "tools/test_step17_frames.cpp" in build)
require("build runs mocked bridge test", "tools/test_step17_bridge.cpp" in build)
require("TARGETS marks native bridge implemented", "Xbox network bridge (B9D) is implemented" in targets)

failed = [name for name, ok in checks if not ok]
for name, ok in checks:
    print(("PASS" if ok else "FAIL") + " | " + name)
print("STEP17 AUDIT %d/%d" % (len(checks)-len(failed), len(checks)))
if failed:
    sys.exit(1)
