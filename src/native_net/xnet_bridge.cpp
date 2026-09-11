#include "native_net/xnet_bridge.h"

#include "diag.h"
#include "native_net/frame_translate.h"

#include <string.h>

namespace it360_native_net {
namespace {

static const DWORD kXnCallerSysApp = 2u;
static const unsigned kFrameMax = 1514u;
static const unsigned kQueueDepth = 64u;

typedef int (*InterceptTransmitCallback)(void* user, const BYTE* data, DWORD length);
typedef int (*InterceptReceiveCallback)(void* user, const BYTE* data, DWORD length);
typedef int (*SetInterceptCallbacksFn)(DWORD caller,
                                       InterceptTransmitCallback transmit,
                                       InterceptReceiveCallback receive,
                                       void* user,
                                       DWORD flags);
typedef int (*InterceptXmitFn)(DWORD caller, BYTE* data, DWORD length);
typedef void (*InterceptRecvFn)(DWORD caller, BYTE* data, DWORD length);
typedef DWORD (*GetEthernetLinkStatusFn)(DWORD caller);

struct FrameSlot {
    BYTE data[kFrameMax];
    DWORD length;
};

struct BridgeState {
    SetInterceptCallbacksFn set_callbacks;
    InterceptXmitFn intercept_xmit;
    InterceptRecvFn intercept_recv;
    GetEthernetLinkStatusFn get_link_status;
    FrameSlot queue[kQueueDepth];
    unsigned head;
    unsigned tail;
    unsigned count;
    volatile LONG queue_lock;
    volatile LONG state_lock;
    volatile LONG initialized;
    volatile LONG registered;
    volatile LONG active;
    volatile LONG native_mac_valid;
    volatile LONG native_mac_announce;
    volatile LONG link_status_announce;
    volatile LONG callback_drops;
    volatile LONG inactive_drops;
    volatile LONG inbound_drops;
    volatile LONG captured_frames;
    volatile LONG injected_frames;
    volatile LONG physical_rx_frames;
    BYTE tether_mac[6];
    BYTE native_mac[6];
};

static BridgeState gBridge;

static bool ResolveXamExport(DWORD ordinal, void** out) {
    it360_platform::ResolveStatus diagnostic;
    if (it360_platform::ResolveModuleOrdinal("xam.xex", ordinal, out, &diagnostic)) return true;
    if (diagnostic.has_status) {
        it360_diag::Log("[iPhoneTether360:B9D] XAM export resolve failed | ordinal=%u operation=%s NTSTATUS=0x%08x\n",
                        static_cast<unsigned>(ordinal),
                        it360_platform::ResolveOperationName(diagnostic.operation),
                        static_cast<unsigned>(diagnostic.status));
    } else {
        it360_diag::Log("[iPhoneTether360:B9D] XAM export resolve failed | ordinal=%u operation=%s status=unavailable\n",
                        static_cast<unsigned>(ordinal),
                        it360_platform::ResolveOperationName(diagnostic.operation));
    }
    return false;
}

static bool TryLock(volatile LONG* lock, unsigned spins) {
    for (unsigned i = 0; i < spins; ++i)
        if (it360_platform::AtomicCompareExchange(lock, 1, 0) == 0) return true;
    return false;
}

static void Unlock(volatile LONG* lock) {
    it360_platform::AtomicExchange(lock, 0);
}

static bool ReadActive() {
    return it360_platform::AtomicCompareExchange(&gBridge.active, 0, 0) != 0;
}

static void FlushQueue() {
    if (!TryLock(&gBridge.queue_lock, 2048u)) return;
    for (unsigned i = 0; i < kQueueDepth; ++i) gBridge.queue[i].length = 0;
    gBridge.head = 0;
    gBridge.tail = 0;
    gBridge.count = 0;
    Unlock(&gBridge.queue_lock);
}

static bool SnapshotMacs(BYTE native_mac[6], BYTE tether_mac[6]) {
    if (!TryLock(&gBridge.state_lock, 256u)) return false;
    const bool valid = it360_platform::AtomicCompareExchange(&gBridge.native_mac_valid, 0, 0) != 0;
    if (valid) memcpy(native_mac, gBridge.native_mac, 6);
    memcpy(tether_mac, gBridge.tether_mac, 6);
    Unlock(&gBridge.state_lock);
    return valid && IsUsableUnicastMac(native_mac) && IsUsableUnicastMac(tether_mac);
}

static bool CaptureNativeMac(const BYTE source[6]) {
    if (!IsUsableUnicastMac(source)) return false;
    if (it360_platform::AtomicCompareExchange(&gBridge.native_mac_valid, 0, 0) != 0) return true;
    if (!TryLock(&gBridge.state_lock, 64u)) return false;
    if (it360_platform::AtomicCompareExchange(&gBridge.native_mac_valid, 0, 0) == 0) {
        memcpy(gBridge.native_mac, source, 6);
        it360_platform::AtomicExchange(&gBridge.native_mac_valid, 1);
        it360_platform::AtomicExchange(&gBridge.native_mac_announce, 1);
    }
    Unlock(&gBridge.state_lock);
    return true;
}

static int XboxTransmitCallback(void*, const BYTE* data, DWORD length) {
    // XAM networking may invoke this on timing-sensitive internal threads.
    // Never log, allocate, sleep, touch USB, or call filesystem/UI here.
    if (!data || length < 14u || length > kFrameMax) {
        it360_platform::AtomicIncrement(&gBridge.callback_drops);
        return 0;
    }
    if (!ReadActive()) {
        it360_platform::AtomicIncrement(&gBridge.inactive_drops);
        return 0;
    }

    if (!CaptureNativeMac(data + 6)) {
        it360_platform::AtomicIncrement(&gBridge.callback_drops);
        return 0;
    }

    if (!TryLock(&gBridge.queue_lock, 64u)) {
        it360_platform::AtomicIncrement(&gBridge.callback_drops);
        return 0;
    }
    if (gBridge.count >= kQueueDepth) {
        Unlock(&gBridge.queue_lock);
        it360_platform::AtomicIncrement(&gBridge.callback_drops);
        return 0;
    }

    FrameSlot* slot = &gBridge.queue[gBridge.tail];
    memcpy(slot->data, data, length);
    slot->length = length;
    gBridge.tail = (gBridge.tail + 1u) % kQueueDepth;
    ++gBridge.count;
    Unlock(&gBridge.queue_lock);
    it360_platform::AtomicIncrement(&gBridge.captured_frames);
    return 0;
}

static int XboxReceiveCallback(void*, const BYTE*, DWORD) {
    // Physical-NIC receive interception is not used as the transport. iPhone
    // frames are injected through export 111 from the tether worker.
    it360_platform::AtomicIncrement(&gBridge.physical_rx_frames);
    return 0;
}

static bool RegisterCallbacks() {
    if (it360_platform::AtomicCompareExchange(&gBridge.registered, 0, 0) != 0) return true;
    if (!gBridge.set_callbacks) return false;

    // Recovered 17559 ABI: ordinal 109, XNCALLER_SYSAPP, callbacks/user/flags.
    // Flags=0 is the base interception mode; no undocumented link override is
    // touched here.
    const int result = gBridge.set_callbacks(kXnCallerSysApp,
                                             &XboxTransmitCallback,
                                             &XboxReceiveCallback,
                                             &gBridge,
                                             0u);
    if (result != 0) {
        it360_diag::Log("[iPhoneTether360:B9D] Ethernet interception registration failed | operation=XamSetEthernetInterceptCallbacks result=0x%08x\n",
                        static_cast<unsigned>(result));
        return false;
    }
    it360_platform::AtomicExchange(&gBridge.registered, 1);
    it360_diag::Log("[iPhoneTether360:B9D] XNet Ethernet interception registered\n");
    return true;
}

} // namespace

bool InitializeBridge() {
    memset(&gBridge, 0, sizeof(gBridge));

    bool ok = true;
    ok = ResolveXamExport(75, (void**)&gBridge.get_link_status) && ok;
    ok = ResolveXamExport(109, (void**)&gBridge.set_callbacks) && ok;
    ok = ResolveXamExport(110, (void**)&gBridge.intercept_xmit) && ok;
    ok = ResolveXamExport(111, (void**)&gBridge.intercept_recv) && ok;
    if (!ok || !gBridge.get_link_status || !gBridge.set_callbacks || !gBridge.intercept_xmit || !gBridge.intercept_recv) {
        memset(&gBridge, 0, sizeof(gBridge));
        it360_diag::Log("[iPhoneTether360:B9D] required XAM Ethernet interception exports unavailable\n");
        return false;
    }

    it360_platform::AtomicExchange(&gBridge.initialized, 1);
    it360_diag::Log("[iPhoneTether360:B9D] XAM Ethernet interception ABI resolved (75/109/110/111)\n");
    return true;
}

void UpdateTransportState(bool transport_ready, bool licence_allows, const BYTE tether_mac[6]) {
    if (it360_platform::AtomicCompareExchange(&gBridge.initialized, 0, 0) == 0) return;
    const bool should_enable = transport_ready && licence_allows && IsUsableUnicastMac(tether_mac);

    if (!should_enable) {
        if (it360_platform::AtomicExchange(&gBridge.active, 0) != 0) {
            FlushQueue();
            it360_diag::Log("[iPhoneTether360:B9D] native Xbox network bridge paused\n");
        }
        return;
    }

    if (!TryLock(&gBridge.state_lock, 2048u)) return;
    const bool tether_changed = memcmp(gBridge.tether_mac, tether_mac, 6) != 0;
    memcpy(gBridge.tether_mac, tether_mac, 6);
    if (tether_changed) {
        // The console-side MAC is stable, but a new iPhone USB Ethernet identity
        // must never inherit stale translation state before first native TX.
        // Keep native_mac if already captured; only the wire-side MAC changes.
    }
    Unlock(&gBridge.state_lock);

    if (!RegisterCallbacks()) {
        it360_platform::AtomicExchange(&gBridge.active, 0);
        return;
    }

    if (it360_platform::AtomicExchange(&gBridge.active, 1) == 0) {
        it360_platform::AtomicExchange(&gBridge.link_status_announce, 1);
        it360_diag::Log("[iPhoneTether360:B9D] native Xbox network bridge active\n");
    }
}

bool InjectIphoneFrame(const BYTE* frame, unsigned length) {
    if (!frame || length < 14u || length > kFrameMax) return false;
    if (!ReadActive() || it360_platform::AtomicCompareExchange(&gBridge.registered, 0, 0) == 0)
        return false;

    BYTE native_mac[6];
    BYTE tether_mac[6];
    if (!SnapshotMacs(native_mac, tether_mac)) return false;

    BYTE copy[kFrameMax];
    memcpy(copy, frame, length);
    if (!TranslateInboundFrame(copy, length, tether_mac, native_mac)) {
        it360_platform::AtomicIncrement(&gBridge.inbound_drops);
        return false;
    }

    // Export 111 continues a received Ethernet frame into XNet. This call is
    // deliberately made from the tether worker, never from the USB callback.
    gBridge.intercept_recv(kXnCallerSysApp, copy, static_cast<DWORD>(length));
    it360_platform::AtomicIncrement(&gBridge.injected_frames);
    return true;
}

bool PopXboxTransmit(BYTE* frame, unsigned capacity, unsigned* length) {
    if (!frame || !length || capacity < kFrameMax) return false;
    *length = 0;
    if (!TryLock(&gBridge.queue_lock, 2048u)) return false;
    if (!gBridge.count) {
        Unlock(&gBridge.queue_lock);
        return false;
    }

    FrameSlot* slot = &gBridge.queue[gBridge.head];
    const unsigned n = static_cast<unsigned>(slot->length);
    if (n < 14u || n > capacity || n > kFrameMax) {
        slot->length = 0;
        gBridge.head = (gBridge.head + 1u) % kQueueDepth;
        --gBridge.count;
        Unlock(&gBridge.queue_lock);
        it360_platform::AtomicIncrement(&gBridge.callback_drops);
        return false;
    }
    memcpy(frame, slot->data, n);
    slot->length = 0;
    gBridge.head = (gBridge.head + 1u) % kQueueDepth;
    --gBridge.count;
    Unlock(&gBridge.queue_lock);

    BYTE native_mac[6];
    BYTE tether_mac[6];
    if (!SnapshotMacs(native_mac, tether_mac) ||
        !TranslateOutboundFrame(frame, n, native_mac, tether_mac)) {
        it360_platform::AtomicIncrement(&gBridge.callback_drops);
        return false;
    }

    *length = n;
    return true;
}

void PhoneDetached() {
    if (it360_platform::AtomicExchange(&gBridge.active, 0) != 0)
        it360_diag::Log("[iPhoneTether360:B9D] iPhone removed; native bridge paused\n");
    FlushQueue();
}

bool IsRegistered() {
    return it360_platform::AtomicCompareExchange(&gBridge.registered, 0, 0) != 0;
}

bool IsActive() {
    return ReadActive();
}

bool HasNativeMac() {
    return it360_platform::AtomicCompareExchange(&gBridge.native_mac_valid, 0, 0) != 0;
}

void ReportDiagnostics() {
    if (it360_platform::AtomicExchange(&gBridge.link_status_announce, 0) != 0 && gBridge.get_link_status) {
        const DWORD status = gBridge.get_link_status(kXnCallerSysApp);
        it360_diag::Log("[iPhoneTether360:B9D] XNet Ethernet link query | operation=XamGetEthernetLinkStatus status=0x%08x active=%u\n",
                        static_cast<unsigned>(status), (status & 0x1u) ? 1u : 0u);
    }

    if (it360_platform::AtomicExchange(&gBridge.native_mac_announce, 0) != 0) {
        BYTE mac[6];
        if (TryLock(&gBridge.state_lock, 2048u)) {
            memcpy(mac, gBridge.native_mac, 6);
            Unlock(&gBridge.state_lock);
            it360_diag::Log("[iPhoneTether360:B9D] native Xbox MAC captured %02x:%02x:%02x:%02x:%02x:%02x\n",
                            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    }

    const LONG callback_drops = it360_platform::AtomicExchange(&gBridge.callback_drops, 0);
    const LONG inactive_drops = it360_platform::AtomicExchange(&gBridge.inactive_drops, 0);
    const LONG inbound_drops = it360_platform::AtomicExchange(&gBridge.inbound_drops, 0);
    const LONG captured = it360_platform::AtomicExchange(&gBridge.captured_frames, 0);
    const LONG injected = it360_platform::AtomicExchange(&gBridge.injected_frames, 0);
    const LONG physical = it360_platform::AtomicExchange(&gBridge.physical_rx_frames, 0);

    if (callback_drops || inbound_drops)
        it360_diag::Log("[iPhoneTether360:B9D] bridge drops tx=%u rx=%u\n",
                        static_cast<unsigned>(callback_drops), static_cast<unsigned>(inbound_drops));
    if (inactive_drops)
        it360_diag::Log("[iPhoneTether360:B9D] native TX ignored while bridge paused=%u\n",
                        static_cast<unsigned>(inactive_drops));
    if (captured || injected)
        it360_diag::Log("[iPhoneTether360:B9D] native frames tx=%u rx=%u\n",
                        static_cast<unsigned>(captured), static_cast<unsigned>(injected));
    if (physical)
        it360_diag::Log("[iPhoneTether360:B9D] physical receive frames observed while intercepted=%u\n",
                        static_cast<unsigned>(physical));
}

} // namespace it360_native_net
