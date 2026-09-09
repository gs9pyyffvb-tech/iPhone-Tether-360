#include <string.h>
#include "ipheth_ncm.h"

namespace it360_net {
namespace {

static unsigned ReadLe16(const BYTE* p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned long ReadLe32(const BYTE* p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}
static unsigned ReadBe16(const BYTE* p) { return ((unsigned)p[0] << 8) | p[1]; }

static unsigned LegacyFrameLength(const BYTE* frame, unsigned cap) {
    if (!frame || cap < 14) return 0;
    const unsigned etherType = ReadBe16(frame + 12);
    if (etherType == 0x0806) return cap >= 42 ? 42 : 0; // Ethernet + ARP/IPv4
    if (etherType == 0x0800 && cap >= 34) {
        const unsigned ihl = (frame[14] & 0x0F) * 4;
        if (ihl < 20 || cap < 14 + ihl) return 0;
        const unsigned ipLen = ReadBe16(frame + 16);
        if (ipLen < ihl || 14 + ipLen > cap) return 0;
        return 14 + ipLen;
    }
    if (etherType == 0x86DD && cap >= 54) {
        const unsigned payload = ReadBe16(frame + 18);
        return 14 + 40 + payload <= cap ? 14 + 40 + payload : 0;
    }
    return 0;
}

static bool DecodeNcm(const BYTE* data, unsigned cap, EtherFrameCallback cb,
                      void* user, unsigned* count, unsigned* announced) {
    // Apple ipheth NCM: NTH16 is 12 bytes and NDP16 follows immediately. Linux
    // reserves a fixed 96-byte NDP area: 8-byte header + 22 DPEs (last is null).
    static const unsigned kNth16 = 12;
    static const unsigned kNdpFixed = 8;
    static const unsigned kMaxDpe = 22;
    static const unsigned kHeaderFloor = 12 + 96;
    static const unsigned long kNcmh = 0x484D434Eu; // USB_CDC_NCM_NTH16_SIGN
    static const unsigned long kNcm0 = 0x304D434Eu; // NDP16 no-CRC

    if (cap < kHeaderFloor || ReadLe32(data) != kNcmh) return false;
    const unsigned headerLen = ReadLe16(data + 4);
    const unsigned blockLen = ReadLe16(data + 8);
    const unsigned ndpIndex = ReadLe16(data + 10);
    if (headerLen != kNth16 || ndpIndex != kNth16 || blockLen < kHeaderFloor || blockLen > cap)
        return false;
    if (ndpIndex + kNdpFixed > blockLen || ReadLe32(data + ndpIndex) != kNcm0) return false;
    const unsigned ndpLen = ReadLe16(data + ndpIndex + 4);
    if (ndpLen < kNdpFixed + 4 || ndpIndex + ndpLen > blockLen) return false;

    unsigned frames = 0;
    const BYTE* dpe = data + ndpIndex + kNdpFixed;
    const unsigned availableDpe = (ndpLen - kNdpFixed) / 4;
    const unsigned loops = availableDpe < kMaxDpe ? availableDpe : kMaxDpe;
    bool sawTerminator = false;
    for (unsigned i = 0; i < loops; ++i, dpe += 4) {
        const unsigned index = ReadLe16(dpe);
        const unsigned len = ReadLe16(dpe + 2);
        if (index == 0 && len == 0) { sawTerminator = true; break; }
        if (index < kHeaderFloor || index >= blockLen || len < 14 || len > blockLen - index)
            return false;
        if (cb && !cb(data + index, len, user)) return false;
        ++frames;
    }
    if (!sawTerminator) return false;
    if (count) *count = frames;
    if (announced) *announced = blockLen;
    return true;
}

} // namespace

bool DecodeIphethRx(const BYTE* data, unsigned capacity, bool ncmEnabled,
                    EtherFrameCallback callback, void* user,
                    unsigned* frameCount, unsigned* announcedLength) {
    if (frameCount) *frameCount = 0;
    if (announcedLength) *announcedLength = 0;
    if (!data || capacity < 4) return false;

    // A zero-length completion leaves our cleared header untouched.
    if (ncmEnabled && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 0) return true;

    // Linux ipheth drops this undocumented 4-byte control frame.
    if (data[0] == 0 && data[1] == 1) {
        if (announcedLength) *announcedLength = 4;
        return true;
    }

    if (ncmEnabled) return DecodeNcm(data, capacity, callback, user, frameCount, announcedLength);

    // Legacy mode carries two alignment bytes before one Ethernet frame. Since
    // the Xbox TRB ABI does not expose a trusted libusb-style actual_length,
    // derive the exact frame length from ARP/IP framing instead of consuming the
    // full receive buffer.
    if (capacity <= 2) return false;
    const BYTE* frame = data + 2;
    const unsigned len = LegacyFrameLength(frame, capacity - 2);
    if (!len) return false;
    if (callback && !callback(frame, len, user)) return false;
    if (frameCount) *frameCount = 1;
    if (announcedLength) *announcedLength = len + 2;
    return true;
}

} // namespace it360_net
