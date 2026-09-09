#pragma once
#include <stddef.h>
#include "platform/xbox_platform.h"

namespace it360_net {

typedef bool (*EtherFrameCallback)(const BYTE* frame, unsigned length, void* user);

// Decode one iPhone tethering Bulk-IN transfer. NCM mode is preferred and
// supports up to 21 Ethernet datagrams per Apple transfer block. A conservative
// legacy fallback is included for devices that reject ENABLE_NCM.
bool DecodeIphethRx(const BYTE* data, unsigned capacity, bool ncmEnabled,
                    EtherFrameCallback callback, void* user,
                    unsigned* frameCount, unsigned* announcedLength);

} // namespace it360_net
