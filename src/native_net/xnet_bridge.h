#pragma once
#include "platform/xbox_platform.h"
namespace it360_native_net {
bool InitializeBridge();
void UpdateTransportState(bool transport_ready, bool licence_allows, const BYTE tether_mac[6]);
bool InjectIphoneFrame(const BYTE* frame, unsigned length);
bool PopXboxTransmit(BYTE* frame, unsigned capacity, unsigned* length);
void PhoneDetached();
bool IsRegistered();
bool IsActive();
bool HasNativeMac();
void ReportDiagnostics();
}
