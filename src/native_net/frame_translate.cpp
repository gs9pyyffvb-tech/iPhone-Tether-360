#include "native_net/frame_translate.h"

#include <string.h>

namespace it360_native_net {
namespace {

static WORD Read16(const BYTE* p) {
    return static_cast<WORD>((static_cast<unsigned>(p[0]) << 8) | p[1]);
}


static void Write16(BYTE* p, WORD v) {
    p[0] = static_cast<BYTE>(v >> 8);
    p[1] = static_cast<BYTE>(v);
}

static unsigned long SumWords(const BYTE* p, unsigned length, unsigned long sum) {
    while (length >= 2u) {
        sum += (static_cast<unsigned>(p[0]) << 8) | p[1];
        p += 2;
        length -= 2u;
    }
    if (length) sum += static_cast<unsigned>(p[0]) << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return sum;
}

static WORD FinishChecksum(unsigned long sum) {
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return static_cast<WORD>(~sum & 0xFFFFu);
}

static WORD UdpChecksum(const BYTE* ip, const BYTE* udp, unsigned udp_length) {
    BYTE pseudo[12];
    memcpy(pseudo, ip + 12, 8);
    pseudo[8] = 0;
    pseudo[9] = 17;
    Write16(pseudo + 10, static_cast<WORD>(udp_length));
    unsigned long sum = SumWords(pseudo, sizeof(pseudo), 0);
    sum = SumWords(udp, udp_length, sum);
    const WORD value = FinishChecksum(sum);
    return value ? value : static_cast<WORD>(0xFFFFu);
}

static bool MacEquals(const BYTE* a, const BYTE* b) {
    return a && b && memcmp(a, b, 6) == 0;
}

static void ReplaceMacIfEqual(BYTE* field, const BYTE from[6], const BYTE to[6]) {
    if (field && MacEquals(field, from)) memcpy(field, to, 6);
}

static bool LocateEtherType(const BYTE* frame, unsigned length, unsigned* l3_offset, WORD* ether_type) {
    if (!frame || length < 14u || !l3_offset || !ether_type) return false;
    unsigned offset = 14u;
    WORD type = Read16(frame + 12);
    // Accept one or two VLAN tags. Xbox 360 traffic is normally untagged, but
    // this makes translation safe if an intercepted path carries 802.1Q/QinQ.
    for (unsigned tags = 0; tags < 2u && (type == 0x8100u || type == 0x88A8u); ++tags) {
        if (length < offset + 4u) return false;
        type = Read16(frame + offset + 2u);
        offset += 4u;
    }
    *l3_offset = offset;
    *ether_type = type;
    return true;
}

static bool LocateDhcp(BYTE* frame, unsigned length, unsigned l3_offset,
                       BYTE** ip_out, BYTE** udp_out, BYTE** bootp_out,
                       unsigned* udp_length_out) {
    if (!frame || length < l3_offset + 20u) return false;
    BYTE* ip = frame + l3_offset;
    if ((ip[0] >> 4) != 4u) return false;
    const unsigned ihl = static_cast<unsigned>(ip[0] & 0x0Fu) * 4u;
    if (ihl < 20u || length < l3_offset + ihl + 8u || ip[9] != 17u) return false;
    const WORD frag = Read16(ip + 6);
    if ((frag & 0x3FFFu) != 0u) return false;
    const unsigned ip_total = Read16(ip + 2);
    if (ip_total < ihl + 8u || l3_offset + ip_total > length) return false;

    BYTE* udp = ip + ihl;
    const unsigned udp_length = Read16(udp + 4);
    if (udp_length < 8u + 240u || udp_length > ip_total - ihl) return false;
    const WORD src_port = Read16(udp);
    const WORD dst_port = Read16(udp + 2);
    if (!((src_port == 68u && dst_port == 67u) ||
          (src_port == 67u && dst_port == 68u))) return false;

    BYTE* bootp = udp + 8;
    if (bootp[1] != 1u || bootp[2] != 6u) return false;
    if (bootp[236] != 99u || bootp[237] != 130u || bootp[238] != 83u || bootp[239] != 99u)
        return false;

    *ip_out = ip;
    *udp_out = udp;
    *bootp_out = bootp;
    *udp_length_out = udp_length;
    return true;
}

static void TranslateDhcpMac(BYTE* frame, unsigned length, unsigned l3_offset,
                             const BYTE from[6], const BYTE to[6]) {
    BYTE* ip = 0;
    BYTE* udp = 0;
    BYTE* bootp = 0;
    unsigned udp_length = 0;
    if (!LocateDhcp(frame, length, l3_offset, &ip, &udp, &bootp, &udp_length)) return;

    ReplaceMacIfEqual(bootp + 28, from, to); // BOOTP chaddr

    // DHCP option 61 (client identifier) is commonly 01:<MAC>. Translating it
    // keeps the DHCP server's identity key consistent with the Ethernet/chaddr
    // identity. Stop safely on malformed options.
    unsigned offset = 240u;
    const unsigned payload_length = udp_length - 8u;
    while (offset < payload_length) {
        const BYTE code = bootp[offset++];
        if (code == 255u) break;
        if (code == 0u) continue;
        if (offset >= payload_length) break;
        const unsigned option_length = bootp[offset++];
        if (offset + option_length > payload_length) break;
        if (code == 61u && option_length == 7u && bootp[offset] == 1u)
            ReplaceMacIfEqual(bootp + offset + 1u, from, to);
        offset += option_length;
    }

    // IPv4 permits a zero UDP checksum. Preserve zero, otherwise recompute
    // after modifying BOOTP/options.
    if (Read16(udp + 6) != 0u) {
        Write16(udp + 6, 0);
        Write16(udp + 6, UdpChecksum(ip, udp, udp_length));
    }
}

static void TranslateArpOutbound(BYTE* arp, unsigned length,
                                 const BYTE native_mac[6], const BYTE tether_mac[6]) {
    if (!arp || length < 28u) return;
    if (Read16(arp) != 1u || Read16(arp + 2) != 0x0800u || arp[4] != 6u || arp[5] != 4u)
        return;
    ReplaceMacIfEqual(arp + 8, native_mac, tether_mac); // sender hardware address
}

static void TranslateArpInbound(BYTE* arp, unsigned length,
                                const BYTE tether_mac[6], const BYTE native_mac[6]) {
    if (!arp || length < 28u) return;
    if (Read16(arp) != 1u || Read16(arp + 2) != 0x0800u || arp[4] != 6u || arp[5] != 4u)
        return;
    ReplaceMacIfEqual(arp + 18, tether_mac, native_mac); // target hardware address
}

} // namespace

bool IsUsableUnicastMac(const BYTE mac[6]) {
    if (!mac || (mac[0] & 1u) != 0u) return false;
    bool any = false;
    for (unsigned i = 0; i < 6u; ++i) if (mac[i] != 0u) any = true;
    return any;
}

bool TranslateOutboundFrame(BYTE* frame, unsigned length,
                            const BYTE native_mac[6], const BYTE tether_mac[6]) {
    if (!frame || length < 14u || !IsUsableUnicastMac(native_mac) || !IsUsableUnicastMac(tether_mac))
        return false;

    ReplaceMacIfEqual(frame + 6, native_mac, tether_mac);

    unsigned l3_offset = 0;
    WORD ether_type = 0;
    if (!LocateEtherType(frame, length, &l3_offset, &ether_type)) return true;
    if (ether_type == 0x0806u) {
        if (length >= l3_offset) TranslateArpOutbound(frame + l3_offset, length - l3_offset, native_mac, tether_mac);
    } else if (ether_type == 0x0800u) {
        TranslateDhcpMac(frame, length, l3_offset, native_mac, tether_mac);
    }
    return true;
}

bool TranslateInboundFrame(BYTE* frame, unsigned length,
                           const BYTE tether_mac[6], const BYTE native_mac[6]) {
    if (!frame || length < 14u || !IsUsableUnicastMac(tether_mac) || !IsUsableUnicastMac(native_mac))
        return false;

    ReplaceMacIfEqual(frame, tether_mac, native_mac);

    unsigned l3_offset = 0;
    WORD ether_type = 0;
    if (!LocateEtherType(frame, length, &l3_offset, &ether_type)) return true;
    if (ether_type == 0x0806u) {
        if (length >= l3_offset) TranslateArpInbound(frame + l3_offset, length - l3_offset, tether_mac, native_mac);
    } else if (ether_type == 0x0800u) {
        TranslateDhcpMac(frame, length, l3_offset, tether_mac, native_mac);
    }
    return true;
}

} // namespace it360_native_net
