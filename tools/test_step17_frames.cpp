#include "native_net/frame_translate.h"

#include <stdio.h>
#include <string.h>

static void W16(BYTE* p, WORD v) { p[0] = (BYTE)(v >> 8); p[1] = (BYTE)v; }
static WORD R16(const BYTE* p) { return (WORD)(((unsigned)p[0] << 8) | p[1]); }

static unsigned long Sum(const BYTE* p, unsigned n, unsigned long sum) {
    while (n >= 2u) { sum += ((unsigned)p[0] << 8) | p[1]; p += 2; n -= 2u; }
    if (n) sum += (unsigned)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return sum;
}

static bool UdpChecksumValid(const BYTE* ip) {
    const unsigned ihl = (ip[0] & 15u) * 4u;
    const BYTE* udp = ip + ihl;
    const unsigned udp_len = R16(udp + 4);
    BYTE pseudo[12];
    memcpy(pseudo, ip + 12, 8);
    pseudo[8] = 0; pseudo[9] = 17; W16(pseudo + 10, (WORD)udp_len);
    unsigned long sum = Sum(pseudo, 12, 0);
    sum = Sum(udp, udp_len, sum);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (sum & 0xFFFFu) == 0xFFFFu;
}

static int Fail(const char* what) {
    fprintf(stderr, "STEP17 FRAME TEST FAIL: %s\n", what);
    return 1;
}

static unsigned MakeDhcp(BYTE* frame, const BYTE eth_src[6], const BYTE eth_dst[6],
                         const BYTE client_mac[6], bool outbound) {
    memset(frame, 0, 512);
    memcpy(frame, eth_dst, 6); memcpy(frame + 6, eth_src, 6); W16(frame + 12, 0x0800);
    BYTE* ip = frame + 14;
    ip[0] = 0x45; ip[8] = 64; ip[9] = 17;
    ip[12] = outbound ? 0 : 172; ip[13] = outbound ? 0 : 20; ip[14] = outbound ? 0 : 10; ip[15] = outbound ? 0 : 1;
    ip[16] = outbound ? 255 : 172; ip[17] = outbound ? 255 : 20; ip[18] = outbound ? 255 : 10; ip[19] = outbound ? 255 : 2;
    BYTE* udp = ip + 20;
    W16(udp, outbound ? 68 : 67); W16(udp + 2, outbound ? 67 : 68);
    BYTE* b = udp + 8;
    b[0] = outbound ? 1 : 2; b[1] = 1; b[2] = 6;
    memcpy(b + 28, client_mac, 6);
    b[236] = 99; b[237] = 130; b[238] = 83; b[239] = 99;
    unsigned o = 240;
    b[o++] = 53; b[o++] = 1; b[o++] = outbound ? 1 : 5;
    b[o++] = 61; b[o++] = 7; b[o++] = 1; memcpy(b + o, client_mac, 6); o += 6;
    b[o++] = 255;
    const unsigned udp_len = 8u + o;
    W16(udp + 4, (WORD)udp_len);
    W16(udp + 6, 0x1234); // force translation to recompute a non-zero checksum
    W16(ip + 2, (WORD)(20u + udp_len));
    return 14u + 20u + udp_len;
}

int main() {
    const BYTE native_mac[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
    const BYTE tether_mac[6] = {0x02,0xAA,0xBB,0xCC,0xDD,0xEE};
    const BYTE gateway_mac[6] = {0x10,0x20,0x30,0x40,0x50,0x60};
    const BYTE broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    BYTE arp[64]; memset(arp, 0, sizeof(arp));
    memcpy(arp, broadcast, 6); memcpy(arp + 6, native_mac, 6); W16(arp + 12, 0x0806);
    BYTE* a = arp + 14; W16(a,1); W16(a+2,0x0800); a[4]=6; a[5]=4; W16(a+6,1);
    memcpy(a+8,native_mac,6);
    if (!it360_native_net::TranslateOutboundFrame(arp, 42, native_mac, tether_mac)) return Fail("ARP outbound rejected");
    if (memcmp(arp+6,tether_mac,6) || memcmp(a+8,tether_mac,6)) return Fail("ARP outbound MAC translation");

    memset(arp, 0, sizeof(arp));
    memcpy(arp, tether_mac, 6); memcpy(arp + 6, gateway_mac, 6); W16(arp + 12, 0x0806);
    a=arp+14; W16(a,1); W16(a+2,0x0800); a[4]=6; a[5]=4; W16(a+6,2);
    memcpy(a+8,gateway_mac,6); memcpy(a+18,tether_mac,6);
    if (!it360_native_net::TranslateInboundFrame(arp,42,tether_mac,native_mac)) return Fail("ARP inbound rejected");
    if (memcmp(arp,native_mac,6) || memcmp(a+18,native_mac,6)) return Fail("ARP inbound MAC translation");

    BYTE dhcp[512];
    unsigned n = MakeDhcp(dhcp, native_mac, broadcast, native_mac, true);
    if (!it360_native_net::TranslateOutboundFrame(dhcp,n,native_mac,tether_mac)) return Fail("DHCP outbound rejected");
    BYTE* ip = dhcp + 14; BYTE* udp = ip + 20; BYTE* b = udp + 8;
    if (memcmp(dhcp+6,tether_mac,6) || memcmp(b+28,tether_mac,6) || memcmp(b+246,tether_mac,6))
        return Fail("DHCP outbound identities");
    if (!UdpChecksumValid(ip)) return Fail("DHCP outbound UDP checksum");

    n = MakeDhcp(dhcp, gateway_mac, tether_mac, tether_mac, false);
    if (!it360_native_net::TranslateInboundFrame(dhcp,n,tether_mac,native_mac)) return Fail("DHCP inbound rejected");
    ip = dhcp + 14; udp = ip + 20; b = udp + 8;
    if (memcmp(dhcp,native_mac,6) || memcmp(b+28,native_mac,6) || memcmp(b+246,native_mac,6))
        return Fail("DHCP inbound identities");
    if (!UdpChecksumValid(ip)) return Fail("DHCP inbound UDP checksum");

    BYTE tcp[64]; memset(tcp,0,sizeof(tcp));
    memcpy(tcp,gateway_mac,6); memcpy(tcp+6,native_mac,6); W16(tcp+12,0x0800);
    if (!it360_native_net::TranslateOutboundFrame(tcp,54,native_mac,tether_mac)) return Fail("generic outbound rejected");
    if (memcmp(tcp+6,tether_mac,6)) return Fail("generic Ethernet source translation");
    memcpy(tcp,tether_mac,6);
    if (!it360_native_net::TranslateInboundFrame(tcp,54,tether_mac,native_mac)) return Fail("generic inbound rejected");
    if (memcmp(tcp,native_mac,6)) return Fail("generic Ethernet destination translation");

    puts("STEP17 FRAME TEST PASS");
    return 0;
}
