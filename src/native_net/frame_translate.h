#pragma once
#include "platform/xbox_platform.h"
namespace it360_native_net {
bool IsUsableUnicastMac(const BYTE mac[6]);
bool TranslateOutboundFrame(BYTE* frame, unsigned length,
                            const BYTE native_mac[6], const BYTE tether_mac[6]);
bool TranslateInboundFrame(BYTE* frame, unsigned length,
                           const BYTE tether_mac[6], const BYTE native_mac[6]);
}
