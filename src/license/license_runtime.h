#pragma once

#include "net_stack.h"
#include "license/license_document.h"
#include "platform/xbox_platform.h"

namespace it360_license_runtime {

enum LicenceState {
    LicenceUnknown = 0,
    LicenceLicensed = 1,
    LicenceFree = 2,
    LicenceUnlicensed = 3
};

enum NetworkGate {
    NetworkGatePending = 0,
    NetworkGateAllow = 1,
    NetworkGateDeny = 2
};

// A Core session starts UNKNOWN. Definitive LICENSED/FREE/UNLICENSED results
// persist until Core is reloaded. UNKNOWN attempts are limited to once per
// physical iPhone connection; a fresh connection may retry.
void ResetCoreSession();
void BeginPhoneConnection(unsigned connection_id);
void EndPhoneConnection(unsigned connection_id);

bool MaybeStartCheck(
    unsigned connection_id,
    const it360_net::NetConfig& config,
    const BYTE gateway_mac[6],
    it360_net::FrameSendFn send,
    void* send_user,
    volatile LONG* independent_internet_ok
);

// Feed every decoded Ethernet frame here as well as to StandaloneNetStack while
// a licence transaction is active. Returns true when the frame is either not
// relevant to licensing or was accepted by the licence TCP stream.
bool OnEthernetFrame(const BYTE* frame, unsigned length);

LicenceState State();
NetworkGate Gate();
bool CheckActive();
bool NativeNetworkAllowed();

} // namespace it360_license_runtime
