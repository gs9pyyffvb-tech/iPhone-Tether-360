#include "platform/xbox_platform.h"
#include <string.h>
#include <stdio.h>
#include "tether_driver.h"
#include "pair_link.h"
#include "ipheth_ncm.h"
#include "net_stack.h"
#include "diag.h"
#include "license/license_runtime.h"
#include "native_net/xnet_bridge.h"

namespace it360_tether {
using namespace iphone_probe;
namespace {

static const WORD kAppleVendor = 0x05AC;
static const BYTE kInterfaceNumber = 2;
static const BYTE kAltSetting = 1;
static const BYTE kClass = 0xFF;
static const BYTE kSubclass = 0xFD;
static const BYTE kProtocol = 0x01;
static const int kBulkType = 2;
static const unsigned kCtrlSize = 0x40;
static const unsigned kRxSize = 65536;
static const unsigned kFrameMax = 1514;
static const unsigned kTxDepth = 16;
static const unsigned kRxFrameDepth = 32;

struct UsbExports {
    UsbdAddDeviceCompleteFn addDeviceComplete;
    UsbdGetDeviceSpeedFn getDeviceSpeed;
    UsbdOpenDefaultEndpointFn openDefaultEndpoint;
    UsbdOpenEndpointFn openEndpoint;
    UsbdQueueAsyncTransferFn queueAsyncTransfer;
    UsbdQueueCloseDefaultEndpointFn queueCloseDefaultEndpoint;
    UsbdQueueCloseEndpointFn queueCloseEndpoint;
    UsbdRemoveDeviceCompleteFn removeDeviceComplete;
    UsbdGetDeviceDescriptorFn getDeviceDescriptor;
    UsbdGetInterfaceDescriptorFn getInterfaceDescriptor;
    UsbdGetConfigurationDescriptorFn getConfigurationDescriptor;
};

struct EndpointInfo {
    BYTE inAddress, outAddress;
    WORD inMaxPacket, outMaxPacket;
    BYTE inInterval, outInterval;
};

enum TetherState {
    TetherIdle, TetherWaitingPair, TetherGetMac, TetherEnableNcm,
    TetherSetInterface, TetherReady, TetherFailed
};

enum ControlOp { CtrlNone, CtrlGetMac, CtrlEnableNcm, CtrlSetInterface, CtrlCarrier };

struct PendingFrame { BYTE data[kFrameMax]; DWORD len; };

struct Context {
    DeviceHandle* handle;
    UsbControlTrb control;
    UsbTrb bulkIn, bulkOut;
    EndpointInfo eps;
    BYTE ctrl[kCtrlSize];
    BYTE rx[kRxSize];
    BYTE tx[kFrameMax];
    PendingFrame q[kTxDepth];
    PendingFrame rxQ[kRxFrameDepth];
    unsigned qHead, qTail, qCount;
    unsigned rxQHead, rxQTail, rxQCount;
    volatile LONG txQueueLock;
    volatile LONG rxQueueLock;
    volatile LONG txBusy;
    bool attached, controlBusy, ncmEnabled, carrierOn;
    bool netStarted, netFailed;
    bool dataCheckingNotified, dataWorkingNotified, reconnectingNotified;
    bool completeNotified, hadWorkingData;
    unsigned rxErrors, rxDrops, workerTicks, networkTicks;
    unsigned connectionId;
    TetherState state;
    ControlOp controlOp;
    BYTE mac[6];
};

static UsbExports gUsb;
static UsbDriverDescriptor17559 gDriver;
static Context gCtx;
static it360_net::StandaloneNetStack gNet;
static volatile LONG gGeneration = 0;
static volatile LONG gWorkerActive = 0;
static volatile LONG gPhoneConnectionSerial = 0;
static volatile LONG gInternetProbeOk = 0;

static bool ResolveUsbExport(DWORD ordinal, void** out) {
    it360_platform::ResolveStatus diagnostic;
    if (it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", ordinal, out, &diagnostic)) return true;
    if (diagnostic.has_status) {
        it360_diag::Log("[iPhoneTether360:B9C] USB export resolve failed | ordinal=%u operation=%s NTSTATUS=0x%08x\n",
                        static_cast<unsigned>(ordinal),
                        it360_platform::ResolveOperationName(diagnostic.operation),
                        static_cast<unsigned>(diagnostic.status));
    } else {
        it360_diag::Log("[iPhoneTether360:B9C] USB export resolve failed | ordinal=%u operation=%s status=unavailable\n",
                        static_cast<unsigned>(ordinal),
                        it360_platform::ResolveOperationName(diagnostic.operation));
    }
    return false;
}

static bool ResolveUsb() {
    memset(&gUsb, 0, sizeof(gUsb));
    bool ok = true;
    ok = ResolveUsbExport(740, (void**)&gUsb.addDeviceComplete) && ok;
    ok = ResolveUsbExport(742, (void**)&gUsb.getDeviceSpeed) && ok;
    ok = ResolveUsbExport(746, (void**)&gUsb.openDefaultEndpoint) && ok;
    ok = ResolveUsbExport(747, (void**)&gUsb.openEndpoint) && ok;
    ok = ResolveUsbExport(748, (void**)&gUsb.queueAsyncTransfer) && ok;
    ok = ResolveUsbExport(749, (void**)&gUsb.queueCloseDefaultEndpoint) && ok;
    ok = ResolveUsbExport(750, (void**)&gUsb.queueCloseEndpoint) && ok;
    ok = ResolveUsbExport(751, (void**)&gUsb.removeDeviceComplete) && ok;
    ok = ResolveUsbExport(759, (void**)&gUsb.getDeviceDescriptor) && ok;
    gUsb.getInterfaceDescriptor = (UsbdGetInterfaceDescriptorFn)0x800D8500u;
    gUsb.getConfigurationDescriptor = (UsbdGetConfigurationDescriptorFn)0x800D83A0u;
    return ok;
}

static bool FindAlt1Endpoints(DeviceHandle* handle, EndpointInfo* out) {
    if (!handle || !out || !gUsb.getConfigurationDescriptor) return false;
    memset(out, 0, sizeof(*out));
    const BYTE* cfg = (const BYTE*)gUsb.getConfigurationDescriptor(handle);
    if (!cfg || cfg[0] < 9 || cfg[1] != 2) return false;
    const unsigned total = cfg[2] | ((unsigned)cfg[3] << 8);
    if (total < 9 || total > 4096) return false;
    const BYTE* p = cfg;
    const BYTE* end = cfg + total;
    bool target = false;
    while (p + 2 <= end) {
        const BYTE len = p[0], type = p[1];
        if (len < 2 || p + len > end) return false;
        if (type == 4 && len >= 9) {
            const UsbInterfaceDescriptor* i = (const UsbInterfaceDescriptor*)p;
            target = i->bInterfaceNumber == kInterfaceNumber && i->bAlternateSetting == kAltSetting &&
                     i->bInterfaceClass == kClass && i->bInterfaceSubClass == kSubclass &&
                     i->bInterfaceProtocol == kProtocol;
        } else if (target && type == 5 && len >= 7) {
            const UsbEndpointDescriptor* e = (const UsbEndpointDescriptor*)p;
            if ((e->bmAttributes & 3) == kBulkType) {
                const WORD m = Swap16(e->wMaxPacketSize) & 0x7FF;
                if (e->bEndpointAddress & 0x80) {
                    out->inAddress = e->bEndpointAddress;
                    out->inMaxPacket = m;
                    out->inInterval = e->bInterval;
                } else {
                    out->outAddress = e->bEndpointAddress;
                    out->outMaxPacket = m;
                    out->outInterval = e->bInterval;
                }
            }
        }
        p += len;
    }
    return out->inAddress && out->outAddress && out->inMaxPacket && out->outMaxPacket;
}

static void Fail(const char* why) {
    gCtx.state = TetherFailed;
    it360_diag::Log("[iPhoneTether360:B9C] FAIL %s\n", why);
    if (gCtx.connectionId)
        it360_diag::NotifyPhone(gCtx.connectionId, it360_diag::PhoneNoticeDataNotWorking,
                                "iPhone Data Not Working");
    else
        it360_diag::Notify("iPhone tether failed - check log");
}

static void FailStatus(const char* why, const char* operation, uint32_t status) {
    gCtx.state = TetherFailed;
    it360_diag::Log("[iPhoneTether360:B9C] FAIL %s | operation=%s status=0x%08x\n",
                    why, operation, static_cast<unsigned>(status));
    if (gCtx.connectionId)
        it360_diag::NotifyPhone(gCtx.connectionId, it360_diag::PhoneNoticeDataNotWorking,
                                "iPhone Data Not Working");
    else
        it360_diag::Notify("iPhone tether failed - check log");
}

static void FormatIp(DWORD ip, char* out, unsigned cap) {
    if (!out || cap < 16) return;
    snprintf(out, cap - 1, "%u.%u.%u.%u",
             (unsigned)((ip >> 24) & 255), (unsigned)((ip >> 16) & 255),
             (unsigned)((ip >> 8) & 255), (unsigned)(ip & 255));
    out[cap - 1] = 0;
}

static bool TryTxQueueLock() {
    for (unsigned i = 0; i < 512u; ++i)
        if (it360_platform::AtomicCompareExchange(&gCtx.txQueueLock, 1, 0) == 0) return true;
    return false;
}

static void UnlockTxQueue() { it360_platform::AtomicExchange(&gCtx.txQueueLock, 0); }

static int QueueRx();
static void PumpTx();
static bool QueueControl(ControlOp op, BYTE requestType, BYTE request, WORD value, WORD index, WORD length);
static uint32_t Worker(void* p);
static bool StartWorker();

static int TxComplete(UsbTrb* trb, int status) {
    if (trb != &gCtx.bulkOut) return status;
    it360_platform::AtomicExchange(&gCtx.txBusy, 0);
    if (status != 0 && gCtx.attached)
        FailStatus("tether Bulk OUT completion error", "USB Bulk OUT completion", static_cast<uint32_t>(status));
    return status;
}

static void PumpTx() {
    if (!gCtx.attached || gCtx.state != TetherReady) return;
    if (it360_platform::AtomicCompareExchange(&gCtx.txBusy, 1, 0) != 0) return;
    if (!TryTxQueueLock()) {
        it360_platform::AtomicExchange(&gCtx.txBusy, 0);
        return;
    }
    if (!gCtx.qCount) {
        UnlockTxQueue();
        it360_platform::AtomicExchange(&gCtx.txBusy, 0);
        return;
    }

    PendingFrame* p = &gCtx.q[gCtx.qHead];
    if (!p->len || p->len > kFrameMax) {
        UnlockTxQueue();
        it360_platform::AtomicExchange(&gCtx.txBusy, 0);
        Fail("invalid Ethernet TX queue entry");
        return;
    }

    memcpy(gCtx.tx, p->data, p->len);
    const DWORD len = p->len;
    p->len = 0;
    gCtx.qHead = (gCtx.qHead + 1u) % kTxDepth;
    --gCtx.qCount;
    UnlockTxQueue();

    gCtx.bulkOut.buffer = gCtx.tx;
    gCtx.bulkOut.length = len;
    gCtx.bulkOut.flags = 1;
    gCtx.bulkOut.callback = it360_platform::Address32(TxComplete);
    gCtx.bulkOut.savedEndpoint = gCtx.bulkOut.endpoint;
    const int q = gUsb.queueAsyncTransfer(gCtx.handle, &gCtx.bulkOut);
    if (q < 0) {
        it360_platform::AtomicExchange(&gCtx.txBusy, 0);
        FailStatus("queue tether Bulk OUT failed", "UsbdQueueAsyncTransfer(Bulk OUT)", static_cast<uint32_t>(q));
    }
}

static bool SendEthernet(const BYTE* frame, unsigned length, void*) {
    if (!frame || length < 14 || length > kFrameMax || !gCtx.attached || gCtx.state != TetherReady)
        return false;
    if (!TryTxQueueLock()) return false;
    if (gCtx.qCount >= kTxDepth) {
        UnlockTxQueue();
        it360_diag::Log("[iPhoneTether360:B9C] Ethernet TX queue full; dropping frame\n");
        return false;
    }
    PendingFrame* p = &gCtx.q[gCtx.qTail];
    memcpy(p->data, frame, length);
    p->len = length;
    gCtx.qTail = (gCtx.qTail + 1u) % kTxDepth;
    ++gCtx.qCount;
    UnlockTxQueue();
    // Worker() is the only USB Bulk OUT submitter. The licence TLS worker may
    // enqueue frames concurrently, but never calls the USB API directly.
    return true;
}

static void NetEvent(it360_net::NetEvent e, const it360_net::NetConfig* cfg, DWORD detail, void*) {
    char a[20], b[20], c[20];
    a[0] = b[0] = c[0] = 0;
    switch (e) {
    case it360_net::NetEventDhcpDiscover:
        it360_diag::Log("[iPhoneTether360:B9C] DHCP Discover sent xid=%08x\n", detail);
        break;
    case it360_net::NetEventDhcpOffer:
        FormatIp(detail, a, sizeof(a));
        it360_diag::Log("[iPhoneTether360:B9C] DHCP Offer %s\n", a);
        break;
    case it360_net::NetEventDhcpBound:
        FormatIp(cfg->ip, a, sizeof(a));
        FormatIp(cfg->gateway, b, sizeof(b));
        FormatIp(cfg->dns, c, sizeof(c));
        it360_diag::Log("[iPhoneTether360:B9C] DHCP ACK IP=%s gateway=%s DNS=%s lease=%us\n",
                        a, b, c, (unsigned)cfg->leaseSeconds);
        break;
    case it360_net::NetEventGatewayResolved:
        FormatIp(detail, a, sizeof(a));
        it360_diag::Log("[iPhoneTether360:B9C] ARP gateway resolved %s\n", a);
        break;
    case it360_net::NetEventIcmpReply:
        FormatIp(detail, a, sizeof(a));
        it360_diag::Log("[iPhoneTether360:B9C] ICMP echo reply from %s\n", a);
        break;
    case it360_net::NetEventDnsResolved:
        FormatIp(detail, a, sizeof(a));
        it360_diag::Log("[iPhoneTether360:B9C] DNS example.com = %s\n", a);
        break;
    case it360_net::NetEventTcpConnected:
        it360_diag::Log("[iPhoneTether360:B9C] TCP/80 handshake complete\n");
        break;
    case it360_net::NetEventHttpResponse:
        it360_platform::AtomicExchange(&gInternetProbeOk, 1);
        gCtx.hadWorkingData = true;
        gCtx.reconnectingNotified = false;
        it360_diag::Log("[iPhoneTether360:B9C] SUCCESS standalone Internet HTTP response received through iPhone\n");
        break;
    case it360_net::NetEventFailure:
        it360_diag::Log("[iPhoneTether360:B9C] standalone network validation failed code=%08x; retry scheduled\n", detail);
        gCtx.netFailed = true;
        break;
    }
}

static bool ConsumeEthernet(const BYTE* frame, unsigned length, void*) {
    if (!frame || length < 14 || length > kFrameMax) return false;
    if (it360_platform::AtomicCompareExchange(&gCtx.rxQueueLock, 1, 0) != 0) {
        ++gCtx.rxDrops;
        return true;
    }
    if (gCtx.rxQCount >= kRxFrameDepth) {
        ++gCtx.rxDrops;
        it360_platform::AtomicExchange(&gCtx.rxQueueLock, 0);
        return true;
    }
    PendingFrame* p = &gCtx.rxQ[gCtx.rxQTail];
    memcpy(p->data, frame, length);
    p->len = length;
    gCtx.rxQTail = (gCtx.rxQTail + 1u) % kRxFrameDepth;
    ++gCtx.rxQCount;
    it360_platform::AtomicExchange(&gCtx.rxQueueLock, 0);
    return true;
}

static bool PopEthernet(PendingFrame* out) {
    if (!out || it360_platform::AtomicCompareExchange(&gCtx.rxQueueLock, 1, 0) != 0) return false;
    if (!gCtx.rxQCount) {
        it360_platform::AtomicExchange(&gCtx.rxQueueLock, 0);
        return false;
    }
    PendingFrame* p = &gCtx.rxQ[gCtx.rxQHead];
    memcpy(out->data, p->data, p->len);
    out->len = p->len;
    p->len = 0;
    gCtx.rxQHead = (gCtx.rxQHead + 1u) % kRxFrameDepth;
    --gCtx.rxQCount;
    it360_platform::AtomicExchange(&gCtx.rxQueueLock, 0);
    return true;
}

static int RxComplete(UsbTrb* trb, int status) {
    if (!gCtx.attached || trb != &gCtx.bulkIn) return status;
    if (status != 0) {
        if (++gCtx.rxErrors > 8) {
            FailStatus("repeated tether Bulk IN errors", "USB Bulk IN completion", static_cast<uint32_t>(status));
            return status;
        }
        it360_diag::Log("[iPhoneTether360:B9C] USB Bulk IN completion nonzero | status=0x%08x retry=%u\n",
                        static_cast<unsigned>(status), gCtx.rxErrors);
    } else {
        unsigned frames = 0, announced = 0;
        if (!it360_net::DecodeIphethRx(gCtx.rx, sizeof(gCtx.rx), gCtx.ncmEnabled,
                                       ConsumeEthernet, 0, &frames, &announced)) {
            if (++gCtx.rxErrors <= 3)
                it360_diag::Log("[iPhoneTether360:B9C] malformed/unknown tether RX block (ncm=%u)\n",
                                gCtx.ncmEnabled ? 1 : 0);
        } else if (frames) {
            gCtx.rxErrors = 0;
        }
    }
    return (gCtx.attached && gCtx.state == TetherReady) ? QueueRx() : 0;
}

static int QueueRx() {
    memset(gCtx.rx, 0, 128);
    gCtx.bulkIn.buffer = gCtx.rx;
    gCtx.bulkIn.length = sizeof(gCtx.rx);
    gCtx.bulkIn.flags = 1;
    gCtx.bulkIn.callback = it360_platform::Address32(RxComplete);
    gCtx.bulkIn.savedEndpoint = gCtx.bulkIn.endpoint;
    const int q = gUsb.queueAsyncTransfer(gCtx.handle, &gCtx.bulkIn);
    if (q < 0)
        FailStatus("queue tether Bulk IN failed", "UsbdQueueAsyncTransfer(Bulk IN)", static_cast<uint32_t>(q));
    return q;
}

static bool OpenDataEndpoints() {
    NTSTATUS st = gUsb.openEndpoint(gCtx.handle, kBulkType, gCtx.eps.inAddress,
                                    gCtx.eps.inMaxPacket, gCtx.eps.inInterval, (DWORD*)&gCtx.bulkIn);
    if (it360_platform::FailedStatus(st)) {
        FailStatus("open tether Bulk IN failed", "UsbdOpenEndpoint(IN)", static_cast<uint32_t>(st));
        return false;
    }
    st = gUsb.openEndpoint(gCtx.handle, kBulkType, gCtx.eps.outAddress,
                           gCtx.eps.outMaxPacket, gCtx.eps.outInterval, (DWORD*)&gCtx.bulkOut);
    if (it360_platform::FailedStatus(st)) {
        FailStatus("open tether Bulk OUT failed", "UsbdOpenEndpoint(OUT)", static_cast<uint32_t>(st));
        return false;
    }
    gCtx.state = TetherReady;
    it360_diag::Log("[iPhoneTether360:B9C] ipheth endpoints ready IN=%02x/%u OUT=%02x/%u NCM=%u\n",
                    gCtx.eps.inAddress, gCtx.eps.inMaxPacket, gCtx.eps.outAddress,
                    gCtx.eps.outMaxPacket, gCtx.ncmEnabled ? 1 : 0);
    if (QueueRx() < 0) return false;
    return QueueControl(CtrlCarrier, 0xC0, 0x45, 0, 2, kCtrlSize);
}

static int ControlComplete(UsbTrb* trb, int status) {
    if (!gCtx.attached || trb != &gCtx.control.trb) return status;
    const ControlOp op = gCtx.controlOp;
    gCtx.controlBusy = false;
    gCtx.controlOp = CtrlNone;

    if (status != 0) {
        if (op == CtrlEnableNcm) {
            gCtx.ncmEnabled = false;
            it360_diag::Log("[iPhoneTether360:B9C] ENABLE_NCM rejected status=%08x; using legacy RX fallback\n", status);
            QueueControl(CtrlSetInterface, 0x01, 0x0B, kAltSetting, kInterfaceNumber, 0);
            return status;
        }
        if (op == CtrlCarrier) {
            it360_diag::Log("[iPhoneTether360:B9C] carrier check status=%08x; will retry\n", status);
            return status;
        }
        FailStatus("tether control request failed", "USB control completion", static_cast<uint32_t>(status));
        return status;
    }

    if (op == CtrlGetMac) {
        bool nonzero = false;
        for (unsigned i = 0; i < 6; ++i) if (gCtx.ctrl[i]) nonzero = true;
        if (!nonzero || (gCtx.ctrl[0] & 1)) {
            Fail("invalid tether MAC response");
            return -1;
        }
        memcpy(gCtx.mac, gCtx.ctrl, 6);
        it360_diag::Log("[iPhoneTether360:B9C] tether MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
                        gCtx.mac[0], gCtx.mac[1], gCtx.mac[2], gCtx.mac[3], gCtx.mac[4], gCtx.mac[5]);
        gCtx.state = TetherEnableNcm;
        QueueControl(CtrlEnableNcm, 0x41, 0x04, 0, 2, 0);
        return 0;
    }
    if (op == CtrlEnableNcm) {
        gCtx.ncmEnabled = true;
        it360_diag::Log("[iPhoneTether360:B9C] Apple NCM RX mode enabled\n");
        gCtx.state = TetherSetInterface;
        QueueControl(CtrlSetInterface, 0x01, 0x0B, kAltSetting, kInterfaceNumber, 0);
        return 0;
    }
    if (op == CtrlSetInterface) {
        it360_diag::Log("[iPhoneTether360:B9C] SET_INTERFACE(2,1) complete\n");
        return OpenDataEndpoints() ? 0 : -1;
    }
    if (op == CtrlCarrier) {
        const bool on = gCtx.ctrl[0] == 0x04 || gCtx.ctrl[1] == 0x04;
        if (on != gCtx.carrierOn) {
            const bool wasOn = gCtx.carrierOn;
            gCtx.carrierOn = on;
            it360_diag::Log("[iPhoneTether360:B9C] Personal Hotspot carrier=%s\n", on ? "ON" : "OFF");
            const bool dataHadWorked = gCtx.hadWorkingData ||
                it360_platform::AtomicCompareExchange(&gInternetProbeOk, 0, 0) != 0;
            if (wasOn && !on && dataHadWorked && !gCtx.reconnectingNotified && gCtx.connectionId) {
                it360_diag::ResetPhoneProgressNotifications(gCtx.connectionId);
                gCtx.reconnectingNotified = true;
                gCtx.completeNotified = false;
                gCtx.dataCheckingNotified = false;
                gCtx.dataWorkingNotified = false;
                it360_platform::AtomicExchange(&gInternetProbeOk, 0);
                it360_diag::NotifyPhone(gCtx.connectionId, it360_diag::PhoneNoticeReconnecting,
                                        "Reconnecting iPhone...");
            }
        }
        return 0;
    }
    return 0;
}

static bool QueueControl(ControlOp op, BYTE requestType, BYTE request, WORD value, WORD index, WORD length) {
    if (!gCtx.attached || !gCtx.control.trb.endpoint || gCtx.controlBusy) return false;
    const DWORD ep0 = gCtx.control.trb.endpoint;
    memset(&gCtx.control, 0, sizeof(gCtx.control));
    memset(gCtx.ctrl, 0, sizeof(gCtx.ctrl));
    gCtx.control.trb.endpoint = ep0;
    gCtx.control.trb.savedEndpoint = ep0;
    gCtx.control.trb.flags = 1;
    gCtx.control.trb.callback = it360_platform::Address32(ControlComplete);
    gCtx.control.trb.buffer = length ? gCtx.ctrl : 0;
    gCtx.control.trb.length = length;
    gCtx.control.packet.bmRequestType = requestType;
    gCtx.control.packet.bRequest = request;
    gCtx.control.packet.wValue = Swap16(value);
    gCtx.control.packet.wIndex = Swap16(index);
    gCtx.control.packet.wLength = Swap16(length);
    gCtx.controlOp = op;
    gCtx.controlBusy = true;
    const int q = gUsb.queueAsyncTransfer(gCtx.handle, &gCtx.control);
    if (q < 0) {
        gCtx.controlBusy = false;
        gCtx.controlOp = CtrlNone;
        it360_diag::Log("[iPhoneTether360:B9C] USB control submit failed | operation=UsbdQueueAsyncTransfer(control) op=%u request=0x%02x result=0x%08x\n",
                        static_cast<unsigned>(op), static_cast<unsigned>(request), static_cast<unsigned>(q));
        return false;
    }
    return true;
}

static void MaybeStartLicenceCheck() {
    if (!gCtx.connectionId || !gCtx.netStarted || !gNet.IsConfigured()) return;
    BYTE gateway_mac[6];
    if (!gNet.CopyGatewayMac(gateway_mac)) return;
    it360_license_runtime::MaybeStartCheck(
        gCtx.connectionId,
        gNet.Config(),
        gateway_mac,
        SendEthernet,
        0,
        &gInternetProbeOk
    );
}

static uint32_t Worker(void* p) {
    const LONG generation = static_cast<LONG>(reinterpret_cast<uintptr_t>(p));
    PendingFrame incoming;
    BYTE native_outgoing[kFrameMax];
    for (;;) {
        it360_platform::SleepMs(50);
        if (it360_platform::AtomicCompareExchange(&gGeneration, 0, 0) != generation || !gCtx.attached) break;
        ++gCtx.workerTicks;

        if (gCtx.state == TetherWaitingPair && !gCtx.controlBusy && it360_link::IsPairReady()) {
            it360_diag::Log("[iPhoneTether360:B9C] pairing ready; starting ipheth control setup\n");
            gCtx.state = TetherGetMac;
            if (!QueueControl(CtrlGetMac, 0xC0, 0x00, 0, 2, kCtrlSize)) Fail("queue GET_MACADDR failed");
        }

        if (gCtx.state == TetherReady) {
            if ((gCtx.workerTicks % 20u) == 0u) {
                if (!gCtx.controlBusy) QueueControl(CtrlCarrier, 0xC0, 0x45, 0, 2, kCtrlSize);
                if (gCtx.netStarted) gNet.TickOneSecond();
                if (gCtx.rxDrops) {
                    it360_diag::Log("[iPhoneTether360:B9C] RX handoff drops=%u\n", gCtx.rxDrops);
                    gCtx.rxDrops = 0;
                }
            }

            if (!gCtx.carrierOn && gCtx.netStarted) {
                gNet.Reset();
                gCtx.netStarted = false;
                gCtx.netFailed = false;
                gCtx.networkTicks = 0;
            }

            if (gCtx.carrierOn && !gCtx.netStarted) {
                const DWORD seed = 0x39430000u | ((DWORD)gCtx.mac[4] << 8) | gCtx.mac[5];
                gCtx.netFailed = false;
                gCtx.networkTicks = 0;
                if (!gCtx.dataCheckingNotified && gCtx.connectionId) {
                    gCtx.dataCheckingNotified = true;
                    it360_diag::NotifyPhone(gCtx.connectionId, it360_diag::PhoneNoticeCheckingData,
                                            "Checking iPhone Data...");
                }
                gCtx.netStarted = gNet.Start(gCtx.mac, seed, SendEthernet, NetEvent, 0);
                if (!gCtx.netStarted)
                    it360_diag::Log("[iPhoneTether360:B9C] could not start DHCP validation stack\n");
            }

            if (gCtx.netStarted) {
                for (unsigned i = 0; i < 16u && PopEthernet(&incoming); ++i) {
                    // Private B9C/TLS consumers see the wire frame first. The
                    // native bridge injects a translated copy afterwards, so
                    // XNet cannot mutate or consume private-stack traffic.
                    (void)it360_license_runtime::OnEthernetFrame(incoming.data, incoming.len);
                    const bool private_ok = gNet.OnEthernetFrame(incoming.data, incoming.len);
                    (void)it360_native_net::InjectIphoneFrame(incoming.data, incoming.len);
                    if (!private_ok) {
                        gCtx.netFailed = true;
                        break;
                    }
                }
                ++gCtx.networkTicks;
                MaybeStartLicenceCheck();
            } else {
                for (unsigned i = 0; i < 16u && PopEthernet(&incoming); ++i) {}
            }

            if (it360_platform::AtomicCompareExchange(&gInternetProbeOk, 0, 0) != 0 &&
                !gCtx.dataWorkingNotified && gCtx.connectionId) {
                gCtx.dataWorkingNotified = true;
                gCtx.hadWorkingData = true;
                gCtx.reconnectingNotified = false;
                it360_diag::NotifyPhone(gCtx.connectionId, it360_diag::PhoneNoticeDataWorking,
                                        "iPhone Data Working");
            }

            // Registration is deferred until the iPhone has a configured L2/L3
            // path and licensing has positively allowed or explicitly failed
            // open. A valid UNLICENSED result therefore never exposes XNet.
            it360_native_net::UpdateTransportState(
                gCtx.carrierOn && gCtx.netStarted && gNet.IsConfigured(),
                it360_license_runtime::NativeNetworkAllowed(),
                gCtx.mac
            );

            if (!gCtx.completeNotified && gCtx.connectionId &&
                it360_platform::AtomicCompareExchange(&gInternetProbeOk, 0, 0) != 0 &&
                !it360_license_runtime::CheckActive() &&
                it360_license_runtime::NativeNetworkAllowed() &&
                it360_native_net::IsActive()) {
                gCtx.completeNotified = true;
                it360_diag::Log("[iPhoneTether360:CORE] iPhone-to-Xbox native network path complete\n");
                it360_diag::NotifyPhone(gCtx.connectionId, it360_diag::PhoneNoticeComplete,
                                        "iPhone to Xbox Complete");
            }

            // Drain XNet's intercepted Ethernet output into the same bounded
            // USB TX queue used by B9C. XAM callbacks never touch USB directly.
            for (unsigned i = 0; i < 8u; ++i) {
                unsigned native_length = 0;
                if (!it360_native_net::PopXboxTransmit(native_outgoing, sizeof(native_outgoing), &native_length))
                    break;
                if (!SendEthernet(native_outgoing, native_length, 0)) break;
            }

            // Single USB TX consumer for frames queued by B9C, licence TLS and
            // the native XNet Ethernet bridge.
            PumpTx();

            if ((gCtx.workerTicks % 20u) == 0u)
                it360_native_net::ReportDiagnostics();

            if (gCtx.netFailed || (gCtx.netStarted && !gNet.InternetValidated() && gCtx.networkTicks > 600u)) {
                it360_diag::Log("[iPhoneTether360:B9C] restarting standalone DHCP/Internet validation\n");
                if (gCtx.hadWorkingData && !gCtx.reconnectingNotified && gCtx.connectionId) {
                    it360_diag::ResetPhoneProgressNotifications(gCtx.connectionId);
                    gCtx.reconnectingNotified = true;
                    it360_diag::NotifyPhone(gCtx.connectionId, it360_diag::PhoneNoticeReconnecting,
                                            "Reconnecting iPhone...");
                }
                gCtx.completeNotified = false;
                gCtx.dataCheckingNotified = false;
                gCtx.dataWorkingNotified = false;
                it360_platform::AtomicExchange(&gInternetProbeOk, 0);
                gNet.Reset();
                gCtx.netStarted = false;
                gCtx.netFailed = false;
                gCtx.networkTicks = 0;
            }
        }
    }

    it360_platform::AtomicExchange(&gWorkerActive, 0);
    if (gCtx.attached) StartWorker();
    return 0;
}

static bool StartWorker() {
    if (it360_platform::AtomicCompareExchange(&gWorkerActive, 1, 0) != 0) return true;
    const LONG generation = it360_platform::AtomicCompareExchange(&gGeneration, 0, 0);
    it360_platform::ThreadStartStatus diagnostic;
    if (!it360_platform::StartDetachedThread(
            Worker,
            reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(generation))),
            &diagnostic)) {
        it360_platform::AtomicExchange(&gWorkerActive, 0);
        if (diagnostic.has_status)
            FailStatus("start tether worker failed", it360_platform::ThreadStartOperationName(diagnostic.operation),
                       static_cast<uint32_t>(diagnostic.status));
        else {
            gCtx.state = TetherFailed;
            it360_diag::Log("[iPhoneTether360:B9C] FAIL start tether worker failed | operation=%s status=unavailable\n",
                            it360_platform::ThreadStartOperationName(diagnostic.operation));
            if (gCtx.connectionId)
                it360_diag::NotifyPhone(gCtx.connectionId, it360_diag::PhoneNoticeDataNotWorking,
                                        "iPhone Data Not Working");
            else
                it360_diag::Notify("iPhone tether failed - check log");
        }
        return false;
    }
    if (diagnostic.close_status_valid && it360_platform::FailedStatus(diagnostic.close_status))
        it360_diag::Log("[iPhoneTether360:B9C] thread handle cleanup failed | operation=NtClose status=0x%08x\n",
                        static_cast<unsigned>(diagnostic.close_status));
    return true;
}

static int AddDevice(DeviceHandle* handle) {
    if (!handle || gCtx.attached) {
        if (handle) gUsb.addDeviceComplete(handle, (int)0xC0000001u);
        return (int)0xC0000001u;
    }

    memset(&gCtx, 0, sizeof(gCtx));
    gCtx.handle = handle;
    gCtx.attached = true;
    gCtx.state = TetherWaitingPair;
    handle->driverExtension = &gCtx;

    if (!FindAlt1Endpoints(handle, &gCtx.eps)) {
        Fail("FF/FD/01 alt1 Bulk endpoints not found");
        handle->driverExtension = 0;
        gCtx.attached = false;
        gUsb.addDeviceComplete(handle, (int)0xC0000001u);
        return (int)0xC0000001u;
    }

    UsbDeviceDescriptor* dev = gUsb.getDeviceDescriptor(handle);
    it360_diag::Log("[iPhoneTether360:B9C] MATCH tether VID=%04x PID=%04x alt1 IN=%02x/%u OUT=%02x/%u\n",
                    dev ? Swap16(dev->idVendor) : 0, dev ? Swap16(dev->idProduct) : 0,
                    gCtx.eps.inAddress, gCtx.eps.inMaxPacket, gCtx.eps.outAddress, gCtx.eps.outMaxPacket);

    gUsb.addDeviceComplete(handle, 0);
    const NTSTATUS st = gUsb.openDefaultEndpoint(handle, (DWORD*)&gCtx.control);
    if (it360_platform::FailedStatus(st)) {
        FailStatus("open tether EP0 failed", "UsbdOpenDefaultEndpoint", static_cast<uint32_t>(st));
        return st;
    }
    if (!gCtx.control.trb.endpoint) {
        it360_diag::Log("[iPhoneTether360:B9C] FAIL open tether EP0 failed | operation=UsbdOpenDefaultEndpoint status=success-with-null-endpoint\n");
        Fail("open tether EP0 failed");
        return (int)0xC0000001u;
    }

    LONG serial = it360_platform::AtomicIncrement(&gPhoneConnectionSerial);
    if (serial <= 0) {
        it360_platform::AtomicExchange(&gPhoneConnectionSerial, 1);
        serial = 1;
    }
    gCtx.connectionId = static_cast<unsigned>(serial);
    it360_platform::AtomicExchange(&gInternetProbeOk, 0);
    it360_diag::BeginPhoneNotifications(gCtx.connectionId);
    it360_diag::NotifyPhone(gCtx.connectionId, it360_diag::PhoneNoticeDetected, "iPhone Detected");
    it360_license_runtime::BeginPhoneConnection(gCtx.connectionId);
    it360_diag::Log("LICENCE | Phone connection session=%u state=%u\n",
                    gCtx.connectionId, static_cast<unsigned>(it360_license_runtime::State()));

    it360_platform::AtomicIncrement(&gGeneration);
    StartWorker();
    return 0;
}

static int RemoveDevice(DeviceHandle* handle) {
    const unsigned connection_id = gCtx.connectionId;
    it360_diag::Log("[iPhoneTether360:B9C] tether interface removed\n");
    gCtx.attached = false;
    it360_platform::AtomicIncrement(&gGeneration);
    it360_license_runtime::EndPhoneConnection(connection_id);
    it360_native_net::PhoneDetached();
    it360_diag::EndPhoneNotifications(connection_id);
    if (connection_id) it360_diag::Notify("iPhone Disconnected");

    if (handle && gCtx.bulkIn.endpoint) {
        const NTSTATUS status = gUsb.queueCloseEndpoint(handle, &gCtx.bulkIn);
        if (it360_platform::FailedStatus(status))
            it360_diag::Log("[iPhoneTether360:B9C] cleanup failed | operation=UsbdQueueCloseEndpoint(IN) NTSTATUS=0x%08x\n",
                            static_cast<unsigned>(status));
    }
    if (handle && gCtx.bulkOut.endpoint) {
        const NTSTATUS status = gUsb.queueCloseEndpoint(handle, &gCtx.bulkOut);
        if (it360_platform::FailedStatus(status))
            it360_diag::Log("[iPhoneTether360:B9C] cleanup failed | operation=UsbdQueueCloseEndpoint(OUT) NTSTATUS=0x%08x\n",
                            static_cast<unsigned>(status));
    }
    if (handle && gCtx.control.trb.endpoint) {
        const NTSTATUS status = gUsb.queueCloseDefaultEndpoint(handle, &gCtx.control);
        if (it360_platform::FailedStatus(status))
            it360_diag::Log("[iPhoneTether360:B9C] cleanup failed | operation=UsbdQueueCloseDefaultEndpoint NTSTATUS=0x%08x\n",
                            static_cast<unsigned>(status));
    }
    if (handle && handle->driverExtension == &gCtx) handle->driverExtension = 0;
    if (handle) {
        const NTSTATUS status = gUsb.removeDeviceComplete(handle);
        if (it360_platform::FailedStatus(status))
            it360_diag::Log("[iPhoneTether360:B9C] cleanup failed | operation=UsbdRemoveDeviceComplete NTSTATUS=0x%08x\n",
                            static_cast<unsigned>(status));
    }
    return 0;
}

static int NodeMatch(UsbInterfaceDescriptor* iface, UsbDeviceDescriptor* dev, void*) {
    return IsTarget(iface, dev) ? 1 : 0;
}

static int Notify(void*) { return 0; }

} // namespace

bool IsTarget(UsbInterfaceDescriptor* iface, UsbDeviceDescriptor* dev) {
    return iface && dev && Swap16(dev->idVendor) == kAppleVendor &&
           iface->bInterfaceNumber == kInterfaceNumber && iface->bAlternateSetting == 0 &&
           iface->bInterfaceClass == kClass && iface->bInterfaceSubClass == kSubclass &&
           iface->bInterfaceProtocol == kProtocol;
}

UsbDriverDescriptor17559* Driver() { return &gDriver; }

bool InitializeDriver() {
    if (!ResolveUsb()) return false;
    if (!it360_native_net::InitializeBridge()) return false;
    memset(&gCtx, 0, sizeof(gCtx));
    gNet.Reset();
    it360_license_runtime::ResetCoreSession();
    it360_platform::AtomicExchange(&gPhoneConnectionSerial, 0);
    it360_platform::AtomicExchange(&gInternetProbeOk, 0);
    memset(&gDriver, 0, sizeof(gDriver));
    gDriver.addDevice = AddDevice;
    gDriver.removeDevice = RemoveDevice;
    gDriver.match = NodeMatch;
    gDriver.notify = Notify;
    it360_diag::Log("[iPhoneTether360:B9C] ipheth FF/FD/01 driver initialized\n");
    it360_diag::Log("[iPhoneTether360:B9D] native bridge initialized; interception deferred until allowed\n");
    it360_diag::Log("LICENCE | Core session state UNKNOWN\n");
    return true;
}

} // namespace it360_tether
