#include "platform/xbox_platform.h"
#include <string.h>
#include <stdio.h>
#include "tether_driver.h"
#include "pair_link.h"
#include "ipheth_ncm.h"
#include "net_stack.h"
#include "diag.h"


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
    volatile LONG rxQueueLock;
    volatile LONG txBusy;
    bool attached, controlBusy, ncmEnabled, carrierOn;
    bool netStarted, netFailed;
    unsigned rxErrors, rxDrops, workerTicks, networkTicks;
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

static bool ResolveUsb() {
    memset(&gUsb, 0, sizeof(gUsb)); bool ok = true;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 740, (void**)&gUsb.addDeviceComplete) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 742, (void**)&gUsb.getDeviceSpeed) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 746, (void**)&gUsb.openDefaultEndpoint) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 747, (void**)&gUsb.openEndpoint) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 748, (void**)&gUsb.queueAsyncTransfer) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 749, (void**)&gUsb.queueCloseDefaultEndpoint) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 750, (void**)&gUsb.queueCloseEndpoint) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 751, (void**)&gUsb.removeDeviceComplete) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 759, (void**)&gUsb.getDeviceDescriptor) && ok;
    gUsb.getInterfaceDescriptor = (UsbdGetInterfaceDescriptorFn)0x800D8500u;
    gUsb.getConfigurationDescriptor = (UsbdGetConfigurationDescriptorFn)0x800D83A0u;
    return ok;
}

static bool FindAlt1Endpoints(DeviceHandle* handle, EndpointInfo* out) {
    if (!handle || !out || !gUsb.getConfigurationDescriptor) return false;
    memset(out, 0, sizeof(*out)); const BYTE* cfg = (const BYTE*)gUsb.getConfigurationDescriptor(handle);
    if (!cfg || cfg[0] < 9 || cfg[1] != 2) return false;
    const unsigned total = cfg[2] | ((unsigned)cfg[3] << 8); if (total < 9 || total > 4096) return false;
    const BYTE* p = cfg; const BYTE* end = cfg + total; bool target = false;
    while (p + 2 <= end) {
        BYTE len = p[0], type = p[1]; if (len < 2 || p + len > end) return false;
        if (type == 4 && len >= 9) {
            const UsbInterfaceDescriptor* i = (const UsbInterfaceDescriptor*)p;
            target = i->bInterfaceNumber == kInterfaceNumber && i->bAlternateSetting == kAltSetting &&
                     i->bInterfaceClass == kClass && i->bInterfaceSubClass == kSubclass &&
                     i->bInterfaceProtocol == kProtocol;
        } else if (target && type == 5 && len >= 7) {
            const UsbEndpointDescriptor* e = (const UsbEndpointDescriptor*)p;
            if ((e->bmAttributes & 3) == kBulkType) {
                WORD m = Swap16(e->wMaxPacketSize) & 0x7FF;
                if (e->bEndpointAddress & 0x80) { out->inAddress=e->bEndpointAddress; out->inMaxPacket=m; out->inInterval=e->bInterval; }
                else { out->outAddress=e->bEndpointAddress; out->outMaxPacket=m; out->outInterval=e->bInterval; }
            }
        }
        p += len;
    }
    return out->inAddress && out->outAddress && out->inMaxPacket && out->outMaxPacket;
}

static void Fail(const char* why) {
    gCtx.state = TetherFailed;
    it360_diag::Log("[iPhoneTether360:B9C] FAIL %s\n", why);
    it360_diag::Notify("iPhone tether failed - check log");
}

static void FormatIp(DWORD ip, char* out, unsigned cap) {
    if (!out || cap < 16) return;
    snprintf(out, cap-1, "%u.%u.%u.%u", (unsigned)((ip>>24)&255), (unsigned)((ip>>16)&255),
             (unsigned)((ip>>8)&255), (unsigned)(ip&255));
    out[cap-1]=0;
}

static int QueueRx();
static void PumpTx();
static bool QueueControl(ControlOp op, BYTE requestType, BYTE request, WORD value, WORD index, WORD length);
static uint32_t Worker(void* p);
static bool StartWorker();

static int TxComplete(UsbTrb* trb, int status) {
    if (trb != &gCtx.bulkOut) return status;
    it360_platform::AtomicExchange(&gCtx.txBusy, 0);
    if (status != 0 && gCtx.attached) {
        it360_diag::Log("[iPhoneTether360:B9C] Bulk OUT completion status=%08x\n", status);
        Fail("tether Bulk OUT completion error");
    }
    // Do not touch the frame queue from the USB completion callback. The 9C
    // worker is the sole TX-queue/network-state owner and will submit the next
    // frame on its next short tick.
    return status;
}

static void PumpTx() {
    if (!gCtx.attached || !gCtx.qCount || gCtx.state != TetherReady) return;
    if (it360_platform::AtomicCompareExchange(&gCtx.txBusy, 1, 0) != 0) return;
    PendingFrame* p = &gCtx.q[gCtx.qHead];
    if (!p->len || p->len > kFrameMax) {
        it360_platform::AtomicExchange(&gCtx.txBusy, 0); Fail("invalid Ethernet TX queue entry"); return;
    }
    memcpy(gCtx.tx, p->data, p->len); DWORD len = p->len; p->len = 0;
    gCtx.qHead = (gCtx.qHead + 1) % kTxDepth; --gCtx.qCount;
    gCtx.bulkOut.buffer = gCtx.tx; gCtx.bulkOut.length = len; gCtx.bulkOut.flags = 1;
    gCtx.bulkOut.callback = it360_platform::Address32(TxComplete); gCtx.bulkOut.savedEndpoint = gCtx.bulkOut.endpoint;
    int q = gUsb.queueAsyncTransfer(gCtx.handle, &gCtx.bulkOut);
    if (q < 0) { it360_platform::AtomicExchange(&gCtx.txBusy, 0); Fail("queue tether Bulk OUT failed"); }
}

static bool SendEthernet(const BYTE* frame, unsigned length, void*) {
    // All StandaloneNetStack entry points run on Worker(), so this queue has a
    // single producer. TxComplete only clears txBusy and never edits qHead/qTail.
    if (!frame || length < 14 || length > kFrameMax || !gCtx.attached || gCtx.state != TetherReady) return false;
    if (gCtx.qCount >= kTxDepth) { it360_diag::Log("[iPhoneTether360:B9C] Ethernet TX queue full; dropping frame\n"); return false; }
    PendingFrame* p = &gCtx.q[gCtx.qTail]; memcpy(p->data, frame, length); p->len = length;
    gCtx.qTail = (gCtx.qTail + 1) % kTxDepth; ++gCtx.qCount; PumpTx(); return true;
}

static void NetEvent(it360_net::NetEvent e, const it360_net::NetConfig* cfg, DWORD detail, void*) {
    char a[20], b[20], c[20]; a[0]=b[0]=c[0]=0;
    switch (e) {
    case it360_net::NetEventDhcpDiscover:
        it360_diag::Log("[iPhoneTether360:B9C] DHCP Discover sent xid=%08x\n", detail); break;
    case it360_net::NetEventDhcpOffer:
        FormatIp(detail,a,sizeof(a)); it360_diag::Log("[iPhoneTether360:B9C] DHCP Offer %s\n",a); break;
    case it360_net::NetEventDhcpBound:
        FormatIp(cfg->ip,a,sizeof(a)); FormatIp(cfg->gateway,b,sizeof(b)); FormatIp(cfg->dns,c,sizeof(c));
        it360_diag::Log("[iPhoneTether360:B9C] DHCP ACK IP=%s gateway=%s DNS=%s lease=%us\n",a,b,c,(unsigned)cfg->leaseSeconds);
        it360_diag::Notify("iPhone USB network has an IP address"); break;
    case it360_net::NetEventGatewayResolved:
        FormatIp(detail,a,sizeof(a)); it360_diag::Log("[iPhoneTether360:B9C] ARP gateway resolved %s\n",a); break;
    case it360_net::NetEventIcmpReply:
        FormatIp(detail,a,sizeof(a)); it360_diag::Log("[iPhoneTether360:B9C] ICMP echo reply from %s\n",a); break;
    case it360_net::NetEventDnsResolved:
        FormatIp(detail,a,sizeof(a)); it360_diag::Log("[iPhoneTether360:B9C] DNS example.com = %s\n",a); break;
    case it360_net::NetEventTcpConnected:
        it360_diag::Log("[iPhoneTether360:B9C] TCP/80 handshake complete\n"); break;
    case it360_net::NetEventHttpResponse:
        it360_diag::Log("[iPhoneTether360:B9C] SUCCESS standalone Internet HTTP response received through iPhone\n");
        it360_diag::Notify("iPhone USB Internet path validated"); break;
    case it360_net::NetEventFailure:
        it360_diag::Log("[iPhoneTether360:B9C] standalone network validation failed code=%08x; retry scheduled\n",detail);
        gCtx.netFailed = true; break;
    }
}

static bool ConsumeEthernet(const BYTE* frame, unsigned length, void*) {
    // USB completions never enter DHCP/ARP/DNS/TCP directly. Copy each decoded
    // Ethernet frame into a bounded hand-off queue and let Worker() own all
    // network state. This prevents callback/timer races on the physical console.
    if (!frame || length < 14 || length > kFrameMax) return false;
    if (it360_platform::AtomicCompareExchange(&gCtx.rxQueueLock, 1, 0) != 0) {
        ++gCtx.rxDrops; return true;
    }
    if (gCtx.rxQCount >= kRxFrameDepth) {
        ++gCtx.rxDrops; it360_platform::AtomicExchange(&gCtx.rxQueueLock, 0); return true;
    }
    PendingFrame* p = &gCtx.rxQ[gCtx.rxQTail]; memcpy(p->data, frame, length); p->len = length;
    gCtx.rxQTail = (gCtx.rxQTail + 1) % kRxFrameDepth; ++gCtx.rxQCount;
    it360_platform::AtomicExchange(&gCtx.rxQueueLock, 0);
    return true;
}

static bool PopEthernet(PendingFrame* out) {
    if (!out || it360_platform::AtomicCompareExchange(&gCtx.rxQueueLock, 1, 0) != 0) return false;
    if (!gCtx.rxQCount) { it360_platform::AtomicExchange(&gCtx.rxQueueLock, 0); return false; }
    PendingFrame* p = &gCtx.rxQ[gCtx.rxQHead];
    memcpy(out->data, p->data, p->len); out->len = p->len; p->len = 0;
    gCtx.rxQHead = (gCtx.rxQHead + 1) % kRxFrameDepth; --gCtx.rxQCount;
    it360_platform::AtomicExchange(&gCtx.rxQueueLock, 0);
    return true;
}

static int RxComplete(UsbTrb* trb, int status) {
    if (!gCtx.attached || trb != &gCtx.bulkIn) return status;
    if (status != 0) {
        it360_diag::Log("[iPhoneTether360:B9C] Bulk IN completion status=%08x\n",status);
        if (++gCtx.rxErrors > 8) { Fail("repeated tether Bulk IN errors"); return status; }
    } else {
        unsigned frames=0, announced=0;
        if (!it360_net::DecodeIphethRx(gCtx.rx, sizeof(gCtx.rx), gCtx.ncmEnabled,
                                       ConsumeEthernet, 0, &frames, &announced)) {
            if (++gCtx.rxErrors <= 3)
                it360_diag::Log("[iPhoneTether360:B9C] malformed/unknown tether RX block (ncm=%u)\n",gCtx.ncmEnabled?1:0);
        } else if (frames) {
            gCtx.rxErrors = 0;
        }
    }
    return (gCtx.attached && gCtx.state == TetherReady) ? QueueRx() : 0;
}

static int QueueRx() {
    // Clear only the header/control-frame area. NCM wBlockLength bounds the rest.
    memset(gCtx.rx, 0, 128);
    gCtx.bulkIn.buffer = gCtx.rx; gCtx.bulkIn.length = sizeof(gCtx.rx); gCtx.bulkIn.flags = 1;
    gCtx.bulkIn.callback = it360_platform::Address32(RxComplete); gCtx.bulkIn.savedEndpoint = gCtx.bulkIn.endpoint;
    int q = gUsb.queueAsyncTransfer(gCtx.handle, &gCtx.bulkIn); if (q < 0) Fail("queue tether Bulk IN failed"); return q;
}

static bool OpenDataEndpoints() {
    NTSTATUS st = gUsb.openEndpoint(gCtx.handle,kBulkType,gCtx.eps.inAddress,gCtx.eps.inMaxPacket,gCtx.eps.inInterval,(DWORD*)&gCtx.bulkIn);
    if (it360_platform::FailedStatus(st)) { Fail("open tether Bulk IN failed"); return false; }
    st = gUsb.openEndpoint(gCtx.handle,kBulkType,gCtx.eps.outAddress,gCtx.eps.outMaxPacket,gCtx.eps.outInterval,(DWORD*)&gCtx.bulkOut);
    if (it360_platform::FailedStatus(st)) { Fail("open tether Bulk OUT failed"); return false; }
    gCtx.state = TetherReady;
    it360_diag::Log("[iPhoneTether360:B9C] ipheth endpoints ready IN=%02x/%u OUT=%02x/%u NCM=%u\n",
                    gCtx.eps.inAddress,gCtx.eps.inMaxPacket,gCtx.eps.outAddress,gCtx.eps.outMaxPacket,gCtx.ncmEnabled?1:0);
    if (QueueRx() < 0) return false;
    return QueueControl(CtrlCarrier,0xC0,0x45,0,2,kCtrlSize);
}

static int ControlComplete(UsbTrb* trb, int status) {
    if (!gCtx.attached || trb != &gCtx.control.trb) return status;
    ControlOp op = gCtx.controlOp; gCtx.controlBusy = false; gCtx.controlOp = CtrlNone;
    if (status != 0) {
        if (op == CtrlEnableNcm) {
            gCtx.ncmEnabled = false;
            it360_diag::Log("[iPhoneTether360:B9C] ENABLE_NCM rejected status=%08x; using legacy RX fallback\n",status);
            QueueControl(CtrlSetInterface,0x01,0x0B,kAltSetting,kInterfaceNumber,0); return status;
        }
        if (op == CtrlCarrier) {
            it360_diag::Log("[iPhoneTether360:B9C] carrier check status=%08x; will retry\n",status); return status;
        }
        Fail("tether control request failed"); return status;
    }
    if (op == CtrlGetMac) {
        bool nonzero=false; for(unsigned i=0;i<6;++i) if(gCtx.ctrl[i]) nonzero=true;
        if (!nonzero || (gCtx.ctrl[0] & 1)) { Fail("invalid tether MAC response"); return -1; }
        memcpy(gCtx.mac,gCtx.ctrl,6);
        it360_diag::Log("[iPhoneTether360:B9C] tether MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
                        gCtx.mac[0],gCtx.mac[1],gCtx.mac[2],gCtx.mac[3],gCtx.mac[4],gCtx.mac[5]);
        gCtx.state=TetherEnableNcm; QueueControl(CtrlEnableNcm,0x41,0x04,0,2,0); return 0;
    }
    if (op == CtrlEnableNcm) {
        gCtx.ncmEnabled=true; it360_diag::Log("[iPhoneTether360:B9C] Apple NCM RX mode enabled\n");
        gCtx.state=TetherSetInterface; QueueControl(CtrlSetInterface,0x01,0x0B,kAltSetting,kInterfaceNumber,0); return 0;
    }
    if (op == CtrlSetInterface) {
        it360_diag::Log("[iPhoneTether360:B9C] SET_INTERFACE(2,1) complete\n"); return OpenDataEndpoints()?0:-1;
    }
    if (op == CtrlCarrier) {
        bool on = gCtx.ctrl[0] == 0x04 || gCtx.ctrl[1] == 0x04;
        if (on != gCtx.carrierOn) {
            gCtx.carrierOn=on;
            it360_diag::Log("[iPhoneTether360:B9C] Personal Hotspot carrier=%s\n",on?"ON":"OFF");
            it360_diag::Notify(on?"iPhone Personal Hotspot connected":"iPhone Personal Hotspot unavailable");
        }
        // Worker() observes carrierOn and performs Start/Reset on the same thread
        // that owns StandaloneNetStack.
        return 0;
    }
    return 0;
}

static bool QueueControl(ControlOp op, BYTE requestType, BYTE request, WORD value, WORD index, WORD length) {
    if (!gCtx.attached || !gCtx.control.trb.endpoint || gCtx.controlBusy) return false;
    DWORD ep0=gCtx.control.trb.endpoint; memset(&gCtx.control,0,sizeof(gCtx.control)); memset(gCtx.ctrl,0,sizeof(gCtx.ctrl));
    gCtx.control.trb.endpoint=ep0; gCtx.control.trb.savedEndpoint=ep0; gCtx.control.trb.flags=1;
    gCtx.control.trb.callback=it360_platform::Address32(ControlComplete); gCtx.control.trb.buffer=length?gCtx.ctrl:0; gCtx.control.trb.length=length;
    gCtx.control.packet.bmRequestType=requestType; gCtx.control.packet.bRequest=request;
    gCtx.control.packet.wValue=Swap16(value); gCtx.control.packet.wIndex=Swap16(index); gCtx.control.packet.wLength=Swap16(length);
    gCtx.controlOp=op; gCtx.controlBusy=true;
    int q=gUsb.queueAsyncTransfer(gCtx.handle,&gCtx.control); if(q<0){gCtx.controlBusy=false;gCtx.controlOp=CtrlNone;return false;} return true;
}

static uint32_t Worker(void* p) {
    LONG generation=static_cast<LONG>(reinterpret_cast<uintptr_t>(p));
    PendingFrame incoming;
    for (;;) {
        it360_platform::SleepMs(50);
        if (it360_platform::AtomicCompareExchange(&gGeneration,0,0)!=generation || !gCtx.attached) break;
        ++gCtx.workerTicks;
        if (gCtx.state==TetherWaitingPair && !gCtx.controlBusy && it360_link::IsPairReady()) {
            it360_diag::Log("[iPhoneTether360:B9C] pairing ready; starting ipheth control setup\n");
            gCtx.state=TetherGetMac;
            if(!QueueControl(CtrlGetMac,0xC0,0x00,0,2,kCtrlSize)) Fail("queue GET_MACADDR failed");
        }
        if (gCtx.state==TetherReady) {
            // Carrier polling and DHCP lease timers are one-second operations;
            // the shorter worker tick keeps async Ethernet TX flowing without
            // making USB completion callbacks own network state.
            if ((gCtx.workerTicks % 20)==0) {
                if (!gCtx.controlBusy) QueueControl(CtrlCarrier,0xC0,0x45,0,2,kCtrlSize);
                if (gCtx.netStarted) gNet.TickOneSecond();
                if (gCtx.rxDrops) {
                    it360_diag::Log("[iPhoneTether360:B9C] RX handoff drops=%u\n", gCtx.rxDrops);
                    gCtx.rxDrops = 0;
                }
            }

            if (!gCtx.carrierOn && gCtx.netStarted) {
                gNet.Reset(); gCtx.netStarted=false; gCtx.netFailed=false; gCtx.networkTicks=0;
            }
            if (gCtx.carrierOn && !gCtx.netStarted) {
                DWORD seed=0x39430000u | ((DWORD)gCtx.mac[4]<<8) | gCtx.mac[5];
                gCtx.netFailed=false; gCtx.networkTicks=0;
                gCtx.netStarted=gNet.Start(gCtx.mac,seed,SendEthernet,NetEvent,0);
                if (!gCtx.netStarted) it360_diag::Log("[iPhoneTether360:B9C] could not start DHCP validation stack\n");
            }

            if (gCtx.netStarted) {
                // Bound the work per tick so a bursty NCM transfer cannot starve
                // carrier polling or detach handling.
                for (unsigned i=0; i<16 && PopEthernet(&incoming); ++i)
                    if (!gNet.OnEthernetFrame(incoming.data, incoming.len)) { gCtx.netFailed=true; break; }
                ++gCtx.networkTicks;
            } else {
                // Discard packets received while carrier/network validation is off.
                for (unsigned i=0; i<16 && PopEthernet(&incoming); ++i) {}
            }
            PumpTx();

            // Retry the standalone validation if packets were lost or a protocol
            // stage explicitly failed. This does not affect the USB link.
            if (gCtx.netFailed || (gCtx.netStarted && !gNet.InternetValidated() && gCtx.networkTicks > 600)) {
                it360_diag::Log("[iPhoneTether360:B9C] restarting standalone DHCP/Internet validation\n");
                gNet.Reset(); gCtx.netStarted=false; gCtx.netFailed=false; gCtx.networkTicks=0;
            }
        }
    }
    it360_platform::AtomicExchange(&gWorkerActive,0);
    // If a new interface arrived while the previous generation was winding
    // down, hand ownership to a new worker rather than leaving it dormant.
    if (gCtx.attached) StartWorker();
    return 0;
}

static bool StartWorker() {
    if (it360_platform::AtomicCompareExchange(&gWorkerActive,1,0)!=0) return true;
    LONG generation=it360_platform::AtomicCompareExchange(&gGeneration,0,0);
    if (!it360_platform::StartDetachedThread(Worker,
            reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(generation))))) {
        it360_platform::AtomicExchange(&gWorkerActive,0);
        Fail("start tether worker failed");
        return false;
    }
    return true;
}

static int AddDevice(DeviceHandle* handle) {
    if (!handle || gCtx.attached) { if(handle) gUsb.addDeviceComplete(handle,(int)0xC0000001u); return (int)0xC0000001u; }
    memset(&gCtx,0,sizeof(gCtx)); gCtx.handle=handle; gCtx.attached=true; gCtx.state=TetherWaitingPair; handle->driverExtension=&gCtx;
    if(!FindAlt1Endpoints(handle,&gCtx.eps)) { Fail("FF/FD/01 alt1 Bulk endpoints not found"); handle->driverExtension=0;gCtx.attached=false;gUsb.addDeviceComplete(handle,(int)0xC0000001u);return (int)0xC0000001u; }
    UsbDeviceDescriptor* dev=gUsb.getDeviceDescriptor(handle);
    it360_diag::Log("[iPhoneTether360:B9C] MATCH tether VID=%04x PID=%04x alt1 IN=%02x/%u OUT=%02x/%u\n",
                    dev?Swap16(dev->idVendor):0,dev?Swap16(dev->idProduct):0,gCtx.eps.inAddress,gCtx.eps.inMaxPacket,gCtx.eps.outAddress,gCtx.eps.outMaxPacket);
    gUsb.addDeviceComplete(handle,0);
    NTSTATUS st=gUsb.openDefaultEndpoint(handle,(DWORD*)&gCtx.control);
    if(it360_platform::FailedStatus(st)||!gCtx.control.trb.endpoint){Fail("open tether EP0 failed");return st;}
    // Deliberately do NOT send SET_CONFIGURATION here. 9B owns device-level
    // configuration; waiting for PairReady guarantees its SET_CONFIGURATION is
    // finished before we switch interface 2 to alt 1.
    it360_platform::AtomicIncrement(&gGeneration);
    StartWorker();
    return 0;
}

static int RemoveDevice(DeviceHandle* handle) {
    it360_diag::Log("[iPhoneTether360:B9C] tether interface removed\n");
    gCtx.attached=false; it360_platform::AtomicIncrement(&gGeneration);
    if(handle&&gCtx.bulkIn.endpoint)gUsb.queueCloseEndpoint(handle,&gCtx.bulkIn);
    if(handle&&gCtx.bulkOut.endpoint)gUsb.queueCloseEndpoint(handle,&gCtx.bulkOut);
    if(handle&&gCtx.control.trb.endpoint)gUsb.queueCloseDefaultEndpoint(handle,&gCtx.control);
    if(handle&&handle->driverExtension==&gCtx)handle->driverExtension=0;
    if (handle) gUsb.removeDeviceComplete(handle);
    // Do not clear gCtx or touch gNet here: RemoveDevice is a USB callback and
    // Worker() may be finishing its current short tick. The generation change
    // makes it exit; the next AddDevice resets the context before a new worker
    // can own it.
    return 0;
}
static int NodeMatch(UsbInterfaceDescriptor* iface, UsbDeviceDescriptor* dev, void*) { return IsTarget(iface,dev)?1:0; }
static int Notify(void*) { return 0; }

} // namespace

bool IsTarget(UsbInterfaceDescriptor* iface, UsbDeviceDescriptor* dev) {
    return iface && dev && Swap16(dev->idVendor)==kAppleVendor &&
           iface->bInterfaceNumber==kInterfaceNumber && iface->bAlternateSetting==0 &&
           iface->bInterfaceClass==kClass && iface->bInterfaceSubClass==kSubclass && iface->bInterfaceProtocol==kProtocol;
}

UsbDriverDescriptor17559* Driver() { return &gDriver; }

bool InitializeDriver() {
    if (!ResolveUsb()) return false;
    memset(&gCtx, 0, sizeof(gCtx));
    gNet.Reset();
    memset(&gDriver, 0, sizeof(gDriver));
    gDriver.addDevice=AddDevice; gDriver.removeDevice=RemoveDevice; gDriver.match=NodeMatch; gDriver.notify=Notify;
    it360_diag::Log("[iPhoneTether360:B9C] ipheth FF/FD/01 driver initialized\n"); return true;
}

} // namespace it360_tether
