#include "platform/xbox_platform.h"
#include <string.h>
#include "usbmux_probe.h"
#include "xbox17559_usb.h"
#include "hook17559.h"
#include "mux_proto.h"
#include "lockdown_probe.h"
#include "pair_session.h"
#include "pair_crypto_provider.h"
#include "pair_store.h"
#include "diag.h"
#include "pair_link.h"
#include "tether_driver.h"


// Mirror all retained B9B diagnostics to the asynchronous persistent logger.
#define DbgPrint it360_diag::Log

namespace iphone_probe {
namespace {

static const WORD kAppleVendor = 0x05AC;
static const BYTE kMuxClass = 0xFF;
static const BYTE kMuxSubclass = 0xFE;
static const BYTE kMuxProtocol = 0x02;
static const int kBulkType = 2;
static const int kDirOut = 0;
static const int kDirIn = 1;
static const DWORD kDynamicMatcherAddress = 0x800D6178u;
static const unsigned kUsbMru = 16384;
static const unsigned kTxMax = 4096;
static const unsigned kTcpPayloadChunk = 3584;
static const unsigned kAppMax = 32768;
static const unsigned kPairFrameMax = 32768;
static const unsigned kTxQueueDepth = 12;
static const WORD kLockdownPort = 62078;
static const WORD kLocalPort = 1;

struct UsbExports {
    UsbdAddDeviceCompleteFn addDeviceComplete;
    UsbdGetDeviceSpeedFn getDeviceSpeed;
    UsbdGetEndpointDescriptorFn getEndpointDescriptor;
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

enum ProbeState {
    ProbeIdle,
    ProbeConfiguring,
    ProbeUsbReady,
    ProbeVersionSent,
    ProbeMuxV2,
    ProbeSynSent,
    ProbeConnected,
    ProbeQuerySent,
    ProbeGetUdidSent,
    ProbeGetWifiSent,
    ProbeValidatePairSent,
    ProbeGetPublicKeySent,
    ProbePairSent,
    ProbeTrustPending,
    ProbePaired,
    ProbeValidated,
    ProbeCryptoBlocked,
    ProbeFailed
};

struct PendingPacket {
    BYTE data[kTxMax];
    DWORD length;
};

struct Context {
    DeviceHandle* handle;
    UsbControlTrb control;
    UsbTrb bulkIn;
    UsbTrb bulkOut;
    BYTE rxBuffer[kUsbMru];
    BYTE txBuffer[kTxMax];
    BYTE buildBuffer[kTxMax];
    bool txBusy;
    PendingPacket queue[kTxQueueDepth];
    unsigned qHead;
    unsigned qTail;
    unsigned qCount;
    ProbeState state;
    bool attached;
    BYTE configurationValue;
    BYTE inAddress;
    BYTE outAddress;
    WORD inMaxPacket;
    WORD outMaxPacket;
    BYTE inInterval;
    BYTE outInterval;

    // usbmux v2 packet sequence numbers
    WORD muxTxSeq;
    WORD muxRxSeq;
    int muxVersion;

    // one synthetic TCP stream to lockdownd
    DWORD tcpTxSeq;
    DWORD tcpAck;
    DWORD tcpWindow;

    BYTE app[kAppMax + 1];
    DWORD appLength;
    DWORD expectedPlist;
    BYTE pairFrame[kPairFrameMax];
    DWORD pairFrameLength;
    char udid[96];
    char wifiAddress[64];
};

static UsbExports gUsb;
static Context gCtx;
static UsbDriverDescriptor17559 gDriver;
static MatcherHook17559 gHook;
static DynamicUsbMatcher17559Fn gOriginalMatcher = 0;
static it360::PairSession gPairSession;
static it360::PairCryptoBackend gPairCrypto;
static bool gPairCryptoReady = false;
static it360::StoredPairRecord gPairRecord;
static bool gPairRecordLoaded = false;
#ifdef IT360_XBOX
static volatile LONG gTrustRetryActive = 0;
static volatile LONG gDeviceGeneration = 0;
#endif

static bool ResolveUsbExports() {
    memset(&gUsb, 0, sizeof(gUsb));
    bool ok = true;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 740, (void**)&gUsb.addDeviceComplete) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 742, (void**)&gUsb.getDeviceSpeed) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 744, (void**)&gUsb.getEndpointDescriptor) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 746, (void**)&gUsb.openDefaultEndpoint) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 747, (void**)&gUsb.openEndpoint) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 748, (void**)&gUsb.queueAsyncTransfer) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 749, (void**)&gUsb.queueCloseDefaultEndpoint) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 750, (void**)&gUsb.queueCloseEndpoint) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 751, (void**)&gUsb.removeDeviceComplete) && ok;
    ok = it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 759, (void**)&gUsb.getDeviceDescriptor) && ok;
    // Internal but already validated in Batch 8 against kernel 17559.
    gUsb.getInterfaceDescriptor = (UsbdGetInterfaceDescriptorFn)0x800D8500u;
    gUsb.getConfigurationDescriptor = (UsbdGetConfigurationDescriptorFn)0x800D83A0u;
    return ok;
}

static bool IsMuxTarget(UsbInterfaceDescriptor* iface, UsbDeviceDescriptor* dev) {
    if (!iface || !dev) return false;
    return Swap16(dev->idVendor) == kAppleVendor &&
           iface->bAlternateSetting == 0 &&
           iface->bInterfaceClass == kMuxClass &&
           iface->bInterfaceSubClass == kMuxSubclass &&
           iface->bInterfaceProtocol == kMuxProtocol;
}

static UsbDriverDescriptor17559* DynamicMatcherHook(
    UsbInterfaceDescriptor* iface, UsbDeviceDescriptor* dev) {
    if (gOriginalMatcher) {
        UsbDriverDescriptor17559* existing = gOriginalMatcher(iface, dev);
        if (existing) return existing;
    }
    if (IsMuxTarget(iface, dev)) {
        DbgPrint("[iPhoneTether360:B9C] MATCH usbmux Apple %04x:%04x iface=%u FF/FE/02\n",
                 Swap16(dev->idVendor), Swap16(dev->idProduct), iface->bInterfaceNumber);
        return &gDriver;
    }
    if (it360_tether::IsTarget(iface, dev)) {
        DbgPrint("[iPhoneTether360:B9C] MATCH Apple tether interface FF/FD/01\n");
        return it360_tether::Driver();
    }
    return 0;
}

static void Fail(const char* why) {
    gCtx.state = ProbeFailed;
    it360_link::SetPairReady(false);
    DbgPrint("[iPhoneTether360:B9C] FAIL %s\n", why);
    it360_diag::Notify("iPhoneTether360: pairing failed - check log");
}

static int QueueRx();
static bool Enqueue(const BYTE* data, DWORD length);
static void PumpTx();
static void StartLockdownTcp();
static void SendQueryType();
static bool SendTcpPayload(const BYTE* payload, DWORD payloadLength);
static void SendGetUniqueDeviceId();
static void SendGetWiFiAddress();
static void SendGetDevicePublicKey();
static void SendValidatePair();
static void StartTrustRetry();
static void ProcessUsbPacket(const BYTE* data, DWORD capacity);
static int StartMuxTransport();

static int ControlComplete(UsbTrb* trb, int status) {
    if (!gCtx.attached || trb != &gCtx.control.trb) return status;
    if (status != 0) {
        Fail("SET_CONFIGURATION completion error");
        return status;
    }
    if (gCtx.state != ProbeConfiguring) {
        Fail("unexpected EP0 completion state");
        return -1;
    }
    DbgPrint("[iPhoneTether360:B9C] SET_CONFIGURATION(%u) ok\n",
             (unsigned)gCtx.configurationValue);
    return StartMuxTransport();
}

static int QueueSetConfiguration() {
    const DWORD ep0 = gCtx.control.trb.endpoint;
    memset(&gCtx.control, 0, sizeof(gCtx.control));
    gCtx.control.trb.endpoint = ep0;
    gCtx.control.trb.savedEndpoint = ep0;
    gCtx.control.trb.flags = 1;
    gCtx.control.trb.callback = it360_platform::Address32(ControlComplete);
    gCtx.control.packet.bmRequestType = 0x00;
    gCtx.control.packet.bRequest = 0x09; // SET_CONFIGURATION
    gCtx.control.packet.wValue = Swap16(gCtx.configurationValue);
    gCtx.control.packet.wIndex = 0;
    gCtx.control.packet.wLength = 0;
    gCtx.state = ProbeConfiguring;
    const int q = gUsb.queueAsyncTransfer(gCtx.handle, &gCtx.control);
    if (q < 0) Fail("queue SET_CONFIGURATION failed");
    return q;
}

static int TxComplete(UsbTrb* trb, int status) {
    if (!gCtx.attached || trb != &gCtx.bulkOut) return status;
    if (status != 0) {
        gCtx.txBusy = false;
        Fail("Bulk OUT completion error");
        return status;
    }
    gCtx.txBusy = false;
    PumpTx();
    return 0;
}

static void PumpTx() {
    if (!gCtx.attached || gCtx.txBusy || gCtx.qCount == 0 || gCtx.state == ProbeFailed) return;
    PendingPacket* p = &gCtx.queue[gCtx.qHead];
    if (!p->length || p->length > kTxMax) {
        Fail("invalid TX queue entry");
        return;
    }
    memcpy(gCtx.txBuffer, p->data, p->length);
    const DWORD len = p->length;
    p->length = 0;
    gCtx.qHead = (gCtx.qHead + 1) % kTxQueueDepth;
    gCtx.qCount--;

    gCtx.bulkOut.buffer = gCtx.txBuffer;
    gCtx.bulkOut.length = len;
    gCtx.bulkOut.flags = 1;
    gCtx.bulkOut.callback = it360_platform::Address32(TxComplete);
    gCtx.bulkOut.savedEndpoint = gCtx.bulkOut.endpoint;
    gCtx.txBusy = true;
    const int q = gUsb.queueAsyncTransfer(gCtx.handle, &gCtx.bulkOut);
    if (q < 0) {
        gCtx.txBusy = false;
        Fail("queue Bulk OUT failed");
    }
}

static bool Enqueue(const BYTE* data, DWORD length) {
    if (!data || !length || length > kTxMax || gCtx.qCount >= kTxQueueDepth) {
        Fail("TX queue overflow/invalid packet");
        return false;
    }
    PendingPacket* p = &gCtx.queue[gCtx.qTail];
    memcpy(p->data, data, length);
    p->length = length;
    gCtx.qTail = (gCtx.qTail + 1) % kTxQueueDepth;
    gCtx.qCount++;
    PumpTx();
    return true;
}

static bool SendSetup() {
    BYTE p[32];
    // usbmuxd resets v2 packet sequence state for SETUP.
    gCtx.muxTxSeq = 0;
    gCtx.muxRxSeq = 0xFFFF;
    const size_t n = iphone_mux::BuildSetupPacket(p, sizeof(p), gCtx.muxTxSeq, gCtx.muxRxSeq);
    if (!n) return false;
    gCtx.muxTxSeq++;
    return Enqueue(p, (DWORD)n);
}

static bool SendTcp(BYTE flags, const BYTE* payload, DWORD payloadLength) {
    const size_t n = iphone_mux::BuildTcpPacket(
        gCtx.buildBuffer, sizeof(gCtx.buildBuffer), gCtx.muxTxSeq, gCtx.muxRxSeq,
        kLocalPort, kLockdownPort, gCtx.tcpTxSeq, gCtx.tcpAck,
        flags, gCtx.tcpWindow, payload, payloadLength);
    if (!n) {
        Fail("TCP packet build failed");
        return false;
    }
    gCtx.muxTxSeq++;
    if (!Enqueue(gCtx.buildBuffer, (DWORD)n)) return false;
    if (payloadLength) gCtx.tcpTxSeq += payloadLength;
    return true;
}

static bool SendTcpPayload(const BYTE* payload, DWORD payloadLength) {
    if (!payload || !payloadLength) return false;
    DWORD sent = 0;
    while (sent < payloadLength) {
        DWORD chunk = payloadLength - sent;
        if (chunk > kTcpPayloadChunk) chunk = kTcpPayloadChunk;
        if (!SendTcp(iphone_mux::kTcpAck, payload + sent, chunk)) return false;
        sent += chunk;
    }
    return true;
}

static void StartLockdownTcp() {
    gCtx.tcpTxSeq = 0;
    gCtx.tcpAck = 0;
    gCtx.tcpWindow = 131072;
    if (SendTcp(iphone_mux::kTcpSyn, 0, 0)) {
        gCtx.state = ProbeSynSent;
        DbgPrint("[iPhoneTether360:B9C] TCP SYN -> lockdownd :62078\n");
    }
}

static void SendQueryType() {
    BYTE frame[1024];
    const size_t frameLen = lockdown_probe::BuildQueryTypeFrame(frame, sizeof(frame));
    if (!frameLen) {
        Fail("QueryType plist build failed");
        return;
    }
    if (SendTcpPayload(frame, (DWORD)frameLen)) {
        gCtx.state = ProbeQuerySent;
        DbgPrint("[iPhoneTether360:B9C] QueryType plist sent (%u bytes)\n", (unsigned)frameLen);
    }
}

static void SendGetUniqueDeviceId() {
    BYTE frame[1024];
    const size_t frameLen = it360::BuildGetUniqueDeviceIdFrame(frame, sizeof(frame));
    if (!frameLen) { Fail("GetValue(UniqueDeviceID) build failed"); return; }
    if (SendTcpPayload(frame, (DWORD)frameLen)) {
        gCtx.state = ProbeGetUdidSent;
        DbgPrint("[iPhoneTether360:B9C] GetValue(UniqueDeviceID) sent\n");
    }
}

static void SendGetWiFiAddress() {
    BYTE frame[1024];
    const size_t frameLen = it360::BuildGetWiFiAddressFrame(frame, sizeof(frame));
    if (!frameLen) { Fail("GetValue(WiFiAddress) build failed"); return; }
    if (SendTcpPayload(frame, (DWORD)frameLen)) {
        gCtx.state = ProbeGetWifiSent;
        DbgPrint("[iPhoneTether360:B9C] GetValue(WiFiAddress) sent\n");
    }
}

static void SendGetDevicePublicKey() {
    BYTE frame[1024];
    const size_t frameLen = gPairSession.Begin(frame, sizeof(frame));
    if (!frameLen) { Fail("GetValue(DevicePublicKey) build/state failed"); return; }
    if (SendTcpPayload(frame, (DWORD)frameLen)) {
        gCtx.state = ProbeGetPublicKeySent;
        DbgPrint("[iPhoneTether360:B9C] GetValue(DevicePublicKey) sent\n");
    }
}

static void SendValidatePair() {
    gCtx.pairFrameLength = (DWORD)it360::BuildValidatePairFrame(
        gPairRecord.identity, gCtx.pairFrame, sizeof(gCtx.pairFrame));
    if (!gCtx.pairFrameLength) { Fail("ValidatePair frame build failed"); return; }
    if (SendTcpPayload(gCtx.pairFrame, gCtx.pairFrameLength)) {
        gCtx.state = ProbeValidatePairSent;
        DbgPrint("[iPhoneTether360:B9C] persisted pair record found; ValidatePair sent\n");
    }
}

#ifdef IT360_XBOX
static uint32_t TrustRetryThread(void* param) {
    const LONG generation = (LONG)(uintptr_t)param;
    // idevicepair's user flow is to retry Pair after the user accepts the dialog.
    // Keep retries finite and do not regenerate identity between attempts.
    for (unsigned attempt = 1; attempt <= 30; ++attempt) {
        it360_platform::SleepMs(2000);
        if (generation != gDeviceGeneration || !gCtx.attached || gCtx.state == ProbeFailed ||
            gCtx.state == ProbePaired || gCtx.state == ProbeValidated) break;
        if (gCtx.state != ProbeTrustPending) continue;
        if (gCtx.txBusy || gCtx.qCount) continue;
        DbgPrint("[iPhoneTether360:B9C] retrying Pair after trust prompt (attempt %u/30)\n", attempt);
        gCtx.state = ProbePairSent;
        if (!SendTcpPayload(gCtx.pairFrame, gCtx.pairFrameLength)) break;
    }
    it360_platform::AtomicExchange(&gTrustRetryActive, 0);
    if (gCtx.attached && gCtx.state == ProbeTrustPending && generation != gDeviceGeneration)
        StartTrustRetry();
    return 0;
}
#endif

static void StartTrustRetry() {
#ifdef IT360_XBOX
    if (it360_platform::AtomicCompareExchange(&gTrustRetryActive, 1, 0) != 0) return;
    const LONG generation = gDeviceGeneration;
    if (!it360_platform::StartDetachedThread(
            TrustRetryThread, reinterpret_cast<void*>(static_cast<uintptr_t>(generation)))) {
        it360_platform::AtomicExchange(&gTrustRetryActive, 0);
        Fail("could not start Pair trust retry worker");
        return;
    }
#endif
}

static void SaveSuccessfulPair(const char* xml) {
    it360::ClearStoredPairRecord(gPairRecord);
    gPairRecord.identity = gPairSession.Identity();
    memcpy(gPairRecord.udid, gCtx.udid, sizeof(gPairRecord.udid));
    gPairRecord.udid[sizeof(gPairRecord.udid)-1] = 0;
    memcpy(gPairRecord.wifi_address, gCtx.wifiAddress, sizeof(gPairRecord.wifi_address));
    gPairRecord.wifi_address[sizeof(gPairRecord.wifi_address)-1] = 0;
    // EscrowBag is optional in the response. Preserve it when supplied so the
    // persisted record is complete enough for later lockdownd/session work.
    it360::XmlExtractDataB64Value(xml, "EscrowBag", gPairRecord.escrow_bag_b64,
                                  sizeof(gPairRecord.escrow_bag_b64));
    if (it360::SavePairRecord(gPairRecord))
        DbgPrint("[iPhoneTether360:B9C] pair record persisted for UDID %s\n", gCtx.udid);
    else
        DbgPrint("[iPhoneTether360:B9C] WARNING Pair succeeded but pair record could not be written\n");
}

static void FallBackFromStoredPair() {
    if (gCtx.udid[0]) it360::DeletePairRecord(gCtx.udid);
    it360::ClearStoredPairRecord(gPairRecord);
    gPairRecordLoaded = false;
    DbgPrint("[iPhoneTether360:B9C] stored pair record rejected; starting fresh Pair flow\n");
    SendGetWiFiAddress();
}

static void HandleCompleteLockdownPlist(char* xml, DWORD xmlLength) {
    if (!xml || !xmlLength || gCtx.state == ProbeFailed) return;

    if (gCtx.state == ProbeQuerySent) {
        if (!lockdown_probe::ResponseContainsLockdownType((const BYTE*)xml, xmlLength)) {
            Fail("QueryType response did not identify com.apple.mobile.lockdown");
            return;
        }
        DbgPrint("[iPhoneTether360:B9C] QueryType = com.apple.mobile.lockdown\n");
        SendGetUniqueDeviceId();
        return;
    }

    if (gCtx.state == ProbeGetUdidSent) {
        if (!it360::ParseGetValueStringXml(xml, gCtx.udid, sizeof(gCtx.udid)) || !gCtx.udid[0]) {
            Fail("UniqueDeviceID GetValue response parse failed");
            return;
        }
        DbgPrint("[iPhoneTether360:B9C] iPhone UDID = %s\n", gCtx.udid);
        it360::ClearStoredPairRecord(gPairRecord);
        gPairRecordLoaded = it360::LoadPairRecord(gCtx.udid, gPairRecord);
        if (gPairRecordLoaded) SendValidatePair();
        else SendGetWiFiAddress();
        return;
    }

    if (gCtx.state == ProbeValidatePairSent) {
        const it360::PairReply reply = it360::ParseValidatePairReplyXml(xml);
        if (reply == it360::PAIR_REPLY_SUCCESS) {
            gCtx.state = ProbeValidated;
            it360_link::SetPairReady(true);
            DbgPrint("[iPhoneTether360:B9C] SUCCESS persisted pairing validated/reused\n");
            it360_diag::Notify("iPhone pairing validated");
        } else {
            // InvalidHostID/InvalidPairRecord is the expected stale-record case.
            // Other ValidatePair errors also fall back to Pair because newer iOS
            // versions primarily validate by starting a session rather than this verb.
            FallBackFromStoredPair();
        }
        return;
    }

    if (gCtx.state == ProbeGetWifiSent) {
        if (!it360::ParseGetValueStringXml(xml, gCtx.wifiAddress, sizeof(gCtx.wifiAddress))) {
            gCtx.wifiAddress[0] = 0;
            DbgPrint("[iPhoneTether360:B9C] WiFiAddress unavailable; continuing Pair flow\n");
        } else {
            DbgPrint("[iPhoneTether360:B9C] WiFiAddress = %s\n", gCtx.wifiAddress);
        }
        SendGetDevicePublicKey();
        return;
    }

    if (gCtx.state == ProbeGetPublicKeySent) {
        if (!gPairCryptoReady) {
            BYTE keyProbe[4096];
            const size_t keyLen = it360::ParseDevicePublicKeyXml(xml, keyProbe, sizeof(keyProbe));
            if (!keyLen) { Fail("DevicePublicKey GetValue response parse failed"); return; }
            gCtx.state = ProbeCryptoBlocked;
            DbgPrint("[iPhoneTether360:B9C] DevicePublicKey received, but Xbox XeCrypt backend is unavailable\n");
            it360_diag::Notify("iPhoneTether360: crypto unavailable - check log");
            return;
        }

        gCtx.pairFrameLength = (DWORD)gPairSession.OnDevicePublicKeyReply(
            xml, gPairCrypto, gCtx.pairFrame, sizeof(gCtx.pairFrame));
        if (!gCtx.pairFrameLength) { Fail("identity generation/Pair frame failed"); return; }
        if (!SendTcpPayload(gCtx.pairFrame, gCtx.pairFrameLength)) return;
        gCtx.state = ProbePairSent;
        DbgPrint("[iPhoneTether360:B9C] Pair request sent (%u bytes; private keys remain host-side)\n",
                 (unsigned)gCtx.pairFrameLength);
        return;
    }

    if (gCtx.state == ProbePairSent || gCtx.state == ProbeTrustPending) {
        const it360::PairReply reply = gPairSession.OnPairReply(xml);
        if (reply == it360::PAIR_REPLY_SUCCESS) {
            SaveSuccessfulPair(xml);
            gCtx.state = ProbePaired;
            it360_link::SetPairReady(true);
            DbgPrint("[iPhoneTether360:B9C] SUCCESS iPhone paired/trusted; tether interface may start\n");
            it360_diag::Notify("iPhone paired and trusted successfully");
        } else if (reply == it360::PAIR_REPLY_PENDING_TRUST) {
            gCtx.state = ProbeTrustPending;
            DbgPrint("[iPhoneTether360:B9C] TRUST PENDING: accept Trust This Computer on iPhone\n");
            it360_diag::Notify("iPhone: tap Trust This Computer");
            StartTrustRetry();
        } else if (reply == it360::PAIR_REPLY_DENIED) {
            Fail("user denied iPhone pairing");
        } else {
            Fail("Pair response error/invalid");
        }
    }
}

static void ConsumeLockdownPayload(const BYTE* p, DWORD n) {
    if (!p || !n || gCtx.state == ProbeFailed || gCtx.state == ProbePaired || gCtx.state == ProbeValidated ||
        gCtx.state == ProbeCryptoBlocked) return;
    if (gCtx.appLength + n > kAppMax) {
        Fail("lockdownd response exceeds stream buffer");
        return;
    }
    memcpy(gCtx.app + gCtx.appLength, p, n);
    gCtx.appLength += n;

    for (;;) {
        if (gCtx.appLength < 4) return;
        gCtx.expectedPlist = lockdown_probe::FramedPlistLength(gCtx.app, gCtx.appLength);
        if (!gCtx.expectedPlist || gCtx.expectedPlist > kAppMax - 4) {
            Fail("invalid framed plist length");
            return;
        }
        if (gCtx.appLength < 4 + gCtx.expectedPlist) return;

        const DWORD consumed = 4 + gCtx.expectedPlist;
        gCtx.app[consumed] = 0;
        DbgPrint("[iPhoneTether360:B9C] lockdownd plist received (%u bytes) state=%u\n",
                 (unsigned)gCtx.expectedPlist, (unsigned)gCtx.state);
        HandleCompleteLockdownPlist((char*)gCtx.app + 4, gCtx.expectedPlist);
        if (gCtx.state == ProbeFailed || gCtx.state == ProbePaired || gCtx.state == ProbeValidated ||
            gCtx.state == ProbeCryptoBlocked) {
            gCtx.appLength = 0;
            gCtx.expectedPlist = 0;
            return;
        }

        const DWORD remain = gCtx.appLength - consumed;
        if (remain) memmove(gCtx.app, gCtx.app + consumed, remain);
        gCtx.appLength = remain;
        gCtx.expectedPlist = 0;
    }
}

static void HandleTcp(const iphone_mux::ParsedTcp& tcp) {
    if (tcp.destPort != kLocalPort || tcp.sourcePort != kLockdownPort) return;

    if (tcp.flags & iphone_mux::kTcpRst) {
        Fail("lockdownd TCP reset");
        return;
    }

    if (gCtx.state == ProbeSynSent) {
        if ((tcp.flags & (iphone_mux::kTcpSyn | iphone_mux::kTcpAck)) !=
            (iphone_mux::kTcpSyn | iphone_mux::kTcpAck)) {
            Fail("lockdownd TCP did not return SYN|ACK");
            return;
        }
        gCtx.tcpTxSeq += 1; // our SYN consumes one sequence number
        gCtx.tcpAck = tcp.sequence + 1; // acknowledge the device's actual ISN
        if (!SendTcp(iphone_mux::kTcpAck, 0, 0)) return;
        gCtx.state = ProbeConnected;
        DbgPrint("[iPhoneTether360:B9C] lockdownd TCP connected\n");
        SendQueryType();
        return;
    }

    if (gCtx.state >= ProbeConnected) {
        bool needAck = false;
        if (tcp.payloadLength) {
            const DWORD endSeq = tcp.sequence + tcp.payloadLength;
            if (endSeq > gCtx.tcpAck) gCtx.tcpAck = endSeq;
            ConsumeLockdownPayload(tcp.payload, tcp.payloadLength);
            needAck = true;
        }
        if (tcp.flags & iphone_mux::kTcpFin) {
            const DWORD finSeq = tcp.sequence + tcp.payloadLength + 1;
            if (finSeq > gCtx.tcpAck) gCtx.tcpAck = finSeq;
            needAck = true;
        }
        if (needAck && gCtx.state != ProbeFailed)
            SendTcp(iphone_mux::kTcpAck, 0, 0);
    }
}

static void ProcessUsbPacket(const BYTE* data, DWORD capacity) {
    if (!data || capacity < 8) return;

    // The packet carries its own total size. This diagnostic deliberately uses
    // small (<16 KiB) requests/responses so we can validate the packet length
    // even if the Xbox TRB does not expose an obvious libusb-style actual_length.
    const DWORD total = iphone_mux::ReadBe32(data + 4);
    if (total < 8 || total > capacity || total > kUsbMru) {
        Fail("invalid usbmux packet length");
        return;
    }

    iphone_mux::ParsedMux mux;
    const bool v2 = gCtx.muxVersion >= 2;
    if (!iphone_mux::ParseMux(data, total, v2, &mux)) {
        Fail("usbmux packet parse failed");
        return;
    }

    if (gCtx.muxVersion >= 2) gCtx.muxRxSeq = mux.rxSeq;

    if (mux.protocol == iphone_mux::kProtoVersion && gCtx.state == ProbeVersionSent) {
        if (mux.payloadLength < 12) {
            Fail("short usbmux version response");
            return;
        }
        const DWORD major = iphone_mux::ReadBe32(mux.payload + 0);
        const DWORD minor = iphone_mux::ReadBe32(mux.payload + 4);
        if (major != 2) {
            Fail("Batch 9B requires usbmux v2");
            return;
        }
        gCtx.muxVersion = (int)major;
        DbgPrint("[iPhoneTether360:B9C] usbmux negotiated v%u.%u\n", major, minor);
        if (major >= 2) {
            if (!SendSetup()) return;
            gCtx.state = ProbeMuxV2;
        }
        StartLockdownTcp();
        return;
    }

    if (mux.protocol == iphone_mux::kProtoTcp && gCtx.state >= ProbeSynSent) {
        iphone_mux::ParsedTcp tcp;
        if (!iphone_mux::ParseTcp(mux, &tcp)) {
            Fail("usbmux TCP parse failed");
            return;
        }
        HandleTcp(tcp);
    }
}

static int RxComplete(UsbTrb* trb, int status) {
    if (!gCtx.attached || trb != &gCtx.bulkIn) return status;
    if (status != 0) {
        Fail("Bulk IN completion error");
        return status;
    }

    // Parse only the length announced by the usbmux header. Re-arm the full
    // receive buffer immediately after handling the packet.
    ProcessUsbPacket(gCtx.rxBuffer, kUsbMru);
    if (gCtx.attached && gCtx.state != ProbeFailed)
        return QueueRx();
    return 0;
}

static int QueueRx() {
    memset(gCtx.rxBuffer, 0, sizeof(gCtx.rxBuffer));
    gCtx.bulkIn.buffer = gCtx.rxBuffer;
    gCtx.bulkIn.length = sizeof(gCtx.rxBuffer);
    gCtx.bulkIn.flags = 1;
    gCtx.bulkIn.callback = it360_platform::Address32(RxComplete);
    gCtx.bulkIn.savedEndpoint = gCtx.bulkIn.endpoint;
    const int q = gUsb.queueAsyncTransfer(gCtx.handle, &gCtx.bulkIn);
    if (q < 0) Fail("queue Bulk IN failed");
    return q;
}

static int StartMuxTransport() {
    NTSTATUS st = gUsb.openEndpoint(gCtx.handle, kBulkType, gCtx.inAddress,
                                   gCtx.inMaxPacket, gCtx.inInterval,
                                   (DWORD*)&gCtx.bulkIn);
    if (it360_platform::FailedStatus(st)) {
        Fail("open usbmux Bulk IN failed");
        return st;
    }
    st = gUsb.openEndpoint(gCtx.handle, kBulkType, gCtx.outAddress,
                           gCtx.outMaxPacket, gCtx.outInterval,
                           (DWORD*)&gCtx.bulkOut);
    if (it360_platform::FailedStatus(st)) {
        Fail("open usbmux Bulk OUT failed");
        return st;
    }

    gCtx.state = ProbeUsbReady;
    DbgPrint("[iPhoneTether360:B9C] usbmux endpoints open IN=%02x/%u OUT=%02x/%u\n",
             gCtx.inAddress, gCtx.inMaxPacket, gCtx.outAddress, gCtx.outMaxPacket);

    if (QueueRx() < 0) return -1;

    BYTE version[32];
    const size_t versionLen = iphone_mux::BuildVersionRequest(version, sizeof(version));
    if (!versionLen || !Enqueue(version, (DWORD)versionLen)) return -1;
    gCtx.state = ProbeVersionSent;
    DbgPrint("[iPhoneTether360:B9C] usbmux v2 negotiation sent\n");
    return 0;
}

static int AddDevice(DeviceHandle* handle) {
    if (!handle || gCtx.attached) {
        if (handle) gUsb.addDeviceComplete(handle, (int)0xC0000001u);
        return (int)0xC0000001u;
    }

    memset(&gCtx, 0, sizeof(gCtx));
    it360_link::SetPairReady(false);
    it360::ClearStoredPairRecord(gPairRecord);
    gPairRecordLoaded = false;
#ifdef IT360_XBOX
    it360_platform::AtomicIncrement(&gDeviceGeneration);
#endif
    gPairSession.Reset();
    memset(&gPairCrypto, 0, sizeof(gPairCrypto));
    gPairCryptoReady = it360::LoadPairCryptoBackend(&gPairCrypto);
    gCtx.handle = handle;
    gCtx.attached = true;
    gCtx.tcpWindow = 131072;
    handle->driverExtension = &gCtx;

    UsbDeviceDescriptor* dev = gUsb.getDeviceDescriptor(handle);
    UsbInterfaceDescriptor* iface = gUsb.getInterfaceDescriptor(handle);
    DbgPrint("[iPhoneTether360:B9C] AddDevice VID=%04x PID=%04x iface=%u alt=%u speed=%d\n",
             dev ? Swap16(dev->idVendor) : 0, dev ? Swap16(dev->idProduct) : 0,
             iface ? iface->bInterfaceNumber : 0xFF, iface ? iface->bAlternateSetting : 0xFF,
             gUsb.getDeviceSpeed(handle));
    DbgPrint("[iPhoneTether360:B9C] Pair crypto backend: %s\n",
             gPairCryptoReady ? "ready" : "unavailable");
    it360_diag::Notify("iPhone detected - starting pairing");

    UsbEndpointDescriptor* in = gUsb.getEndpointDescriptor(handle, 0, kBulkType, kDirIn);
    UsbEndpointDescriptor* out = gUsb.getEndpointDescriptor(handle, 0, kBulkType, kDirOut);
    UsbConfigurationDescriptor* cfg = gUsb.getConfigurationDescriptor(handle);
    if (!in || !out || !cfg || !cfg->bConfigurationValue) {
        Fail("FF/FE/02 descriptors/configuration not found");
        handle->driverExtension = 0;
        gCtx.attached = false;
        gUsb.addDeviceComplete(handle, (int)0xC0000001u);
        return (int)0xC0000001u;
    }

    gCtx.configurationValue = cfg->bConfigurationValue;
    gCtx.inAddress = in->bEndpointAddress;
    gCtx.outAddress = out->bEndpointAddress;
    gCtx.inMaxPacket = Swap16(in->wMaxPacketSize) & 0x7FF;
    gCtx.outMaxPacket = Swap16(out->wMaxPacketSize) & 0x7FF;
    gCtx.inInterval = in->bInterval;
    gCtx.outInterval = out->bInterval;

    DbgPrint("[iPhoneTether360:B9C] descriptors cfg=%u IN=%02x/%u OUT=%02x/%u\n",
             gCtx.configurationValue, gCtx.inAddress, gCtx.inMaxPacket,
             gCtx.outAddress, gCtx.outMaxPacket);

    gUsb.addDeviceComplete(handle, 0);

    NTSTATUS st = gUsb.openDefaultEndpoint(handle, (DWORD*)&gCtx.control);
    if (it360_platform::FailedStatus(st) || !gCtx.control.trb.endpoint) {
        Fail("open EP0 failed");
        return st;
    }
    DbgPrint("[iPhoneTether360:B9C] EP0=%08x; sending SET_CONFIGURATION(%u)\n",
             gCtx.control.trb.endpoint, (unsigned)gCtx.configurationValue);
    return QueueSetConfiguration();
}

static int RemoveDevice(DeviceHandle* handle) {
    DbgPrint("[iPhoneTether360:B9C] RemoveDevice state=%u\n", (unsigned)gCtx.state);
    gCtx.attached = false;
    it360_link::SetPairReady(false);
#ifdef IT360_XBOX
    it360_platform::AtomicIncrement(&gDeviceGeneration);
#endif
    if (handle && gCtx.bulkIn.endpoint) gUsb.queueCloseEndpoint(handle, &gCtx.bulkIn);
    if (handle && gCtx.bulkOut.endpoint) gUsb.queueCloseEndpoint(handle, &gCtx.bulkOut);
    if (handle && gCtx.control.trb.endpoint) gUsb.queueCloseDefaultEndpoint(handle, &gCtx.control);
    if (handle && handle->driverExtension == &gCtx) handle->driverExtension = 0;
    if (handle) gUsb.removeDeviceComplete(handle);
    memset(&gCtx, 0, sizeof(gCtx));
    return 0;
}

static int NodeMatch(UsbInterfaceDescriptor* iface, UsbDeviceDescriptor* dev, void*) {
    return IsMuxTarget(iface, dev) ? 1 : 0;
}
static int Notify(void*) { return 0; }

} // namespace

bool InstallLockdownProbe() {
    if (!ResolveUsbExports()) {
        DbgPrint("[iPhoneTether360:B9C] FAIL resolving USB exports\n");
        return false;
    }
    memset(&gCtx, 0, sizeof(gCtx));
    memset(&gDriver, 0, sizeof(gDriver));
    if (!it360_tether::InitializeDriver()) {
        DbgPrint("[iPhoneTether360:B9C] FAIL initializing ipheth tether driver\n");
        return false;
    }
    gDriver.addDevice = AddDevice;
    gDriver.removeDevice = RemoveDevice;
    gDriver.match = NodeMatch;
    gDriver.notify = Notify;

    if (!gHook.Install((void*)kDynamicMatcherAddress, (void*)DynamicMatcherHook)) {
        DbgPrint("[iPhoneTether360:B9C] FAIL matcher hook/prologue mismatch\n");
        return false;
    }
    gOriginalMatcher = (DynamicUsbMatcher17559Fn)gHook.Original();
    DbgPrint("[iPhoneTether360:B9C] READY; unplug/replug iPhone\n");
    return true;
}

} // namespace iphone_probe
