#include <string.h>
#include "net_stack.h"

namespace it360_net {
namespace {

static WORD R16(const BYTE* p) { return (WORD)(((unsigned)p[0] << 8) | p[1]); }
static DWORD R32(const BYTE* p) {
    return (DWORD)(((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | (DWORD)p[3]);
}
static void W16(BYTE* p, WORD v) { p[0] = (BYTE)(v >> 8); p[1] = (BYTE)v; }
static void W32(BYTE* p, DWORD v) {
    p[0] = (BYTE)(v >> 24); p[1] = (BYTE)(v >> 16); p[2] = (BYTE)(v >> 8); p[3] = (BYTE)v;
}
static bool MacEq(const BYTE* a, const BYTE* b) { return memcmp(a, b, 6) == 0; }
static unsigned long SumWords(const BYTE* p, unsigned n, unsigned long sum) {
    while (n >= 2) { sum += ((unsigned)p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) sum += (unsigned)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return sum;
}
static WORD FinishChecksum(unsigned long sum) {
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (WORD)(~sum & 0xFFFFu);
}
static WORD Checksum(const BYTE* p, unsigned n) { return FinishChecksum(SumWords(p, n, 0)); }
static WORD TransportChecksum(DWORD src, DWORD dst, BYTE proto, const BYTE* p, unsigned n) {
    BYTE pseudo[12];
    W32(pseudo, src); W32(pseudo + 4, dst); pseudo[8] = 0; pseudo[9] = proto; W16(pseudo + 10, (WORD)n);
    unsigned long sum = SumWords(pseudo, sizeof(pseudo), 0);
    sum = SumWords(p, n, sum);
    WORD c = FinishChecksum(sum);
    return c ? c : (WORD)0xFFFF;
}
static void Broadcast(BYTE mac[6]) { memset(mac, 0xFF, 6); }
static bool IsZeroMac(const BYTE* m) {
    for (unsigned i = 0; i < 6; ++i) if (m[i]) return false;
    return true;
}
static bool IsBroadcastMac(const BYTE* m) {
    for (unsigned i = 0; i < 6; ++i) if (m[i] != 0xFF) return false;
    return true;
}
static bool ContainsHttp(const BYTE* p, unsigned n) {
    static const char k[] = "HTTP/";
    for (unsigned i = 0; i + 5 <= n; ++i) if (memcmp(p + i, k, 5) == 0) return true;
    return false;
}
static unsigned SkipDnsName(const BYTE* p, unsigned n, unsigned off) {
    unsigned guard = 0;
    while (off < n && guard++ < 128) {
        BYTE c = p[off];
        if (c == 0) return off + 1;
        if ((c & 0xC0) == 0xC0) return off + 2 <= n ? off + 2 : 0;
        if (c > 63 || off + 1 + c > n) return 0;
        off += 1 + c;
    }
    return 0;
}

} // namespace

StandaloneNetStack::StandaloneNetStack() { Reset(); }

void StandaloneNetStack::Reset() {
    memset(&cfg_, 0, sizeof(cfg_));
    send_ = 0; event_ = 0; user_ = 0; seed_ = 0; xid_ = 0;
    offeredIp_ = 0; offerServer_ = 0; resolvedIp_ = 0;
    memset(gatewayMac_, 0, sizeof(gatewayMac_));
    gatewayMacValid_ = false; configured_ = false; internetOk_ = false;
    dhcpRequested_ = false; renewing_ = false; dhcpSeconds_ = 0; leaseAgeSeconds_ = 0;
    dnsId_ = 0; ipId_ = 1; tcpLocalPort_ = 0;
    tcpSeq_ = 0; tcpAck_ = 0; tcpSynSent_ = false; tcpEstablished_ = false; httpSent_ = false;
}

bool StandaloneNetStack::Start(const BYTE mac[6], DWORD seed, FrameSendFn send, NetEventFn event, void* user) {
    Reset();
    if (!mac || IsZeroMac(mac) || !send) return false;
    memcpy(cfg_.mac, mac, 6);
    send_ = send; event_ = event; user_ = user; seed_ = seed ? seed : 0x39C00001u;
    xid_ = (seed_ ^ ((DWORD)mac[2] << 24) ^ ((DWORD)mac[3] << 16) ^ ((DWORD)mac[4] << 8) ^ mac[5]) & 0xFFFFFFFFu;
    dnsId_ = (WORD)((xid_ ^ (xid_ >> 16)) & 0xFFFFu);
    if (!dnsId_) dnsId_ = 0x3943;
    tcpLocalPort_ = (WORD)(49152u + (seed_ & 0x0FFFu));
    tcpSeq_ = (seed_ ^ 0xA5C39B17u) & 0xFFFFFFFFu;
    return SendDhcpDiscover();
}

void StandaloneNetStack::Event(NetEvent e, DWORD detail) { if (event_) event_(e, &cfg_, detail, user_); }
bool StandaloneNetStack::SendFrame(const BYTE* frame, unsigned len) { return send_ && frame && len >= 14 && send_(frame, len, user_); }

bool StandaloneNetStack::SendDhcpDiscover() {
    dhcpRequested_ = false; renewing_ = false; dhcpSeconds_ = 0;
    BYTE bootp[548]; memset(bootp, 0, sizeof(bootp));
    bootp[0] = 1; bootp[1] = 1; bootp[2] = 6; bootp[3] = 0;
    W32(bootp + 4, xid_); W16(bootp + 10, 0x8000); memcpy(bootp + 28, cfg_.mac, 6);
    bootp[236] = 99; bootp[237] = 130; bootp[238] = 83; bootp[239] = 99;
    unsigned o = 240;
    bootp[o++] = 53; bootp[o++] = 1; bootp[o++] = 1; // Discover
    bootp[o++] = 61; bootp[o++] = 7; bootp[o++] = 1; memcpy(bootp + o, cfg_.mac, 6); o += 6;
    bootp[o++] = 55; bootp[o++] = 7; bootp[o++] = 1; bootp[o++] = 3; bootp[o++] = 6;
    bootp[o++] = 15; bootp[o++] = 51; bootp[o++] = 54; bootp[o++] = 58;
    bootp[o++] = 255;
    BYTE bcast[6]; Broadcast(bcast);
    Event(NetEventDhcpDiscover, xid_);
    return SendIpv4Udp(0xFFFFFFFFu, 68, 67, bootp, o, bcast);
}

bool StandaloneNetStack::SendDhcpRequest() {
    if (!offeredIp_ || !offerServer_) return false;
    renewing_ = false; dhcpSeconds_ = 0;
    BYTE bootp[548]; memset(bootp, 0, sizeof(bootp));
    bootp[0] = 1; bootp[1] = 1; bootp[2] = 6; W32(bootp + 4, xid_); W16(bootp + 10, 0x8000);
    memcpy(bootp + 28, cfg_.mac, 6);
    bootp[236] = 99; bootp[237] = 130; bootp[238] = 83; bootp[239] = 99;
    unsigned o = 240;
    bootp[o++] = 53; bootp[o++] = 1; bootp[o++] = 3; // Request
    bootp[o++] = 50; bootp[o++] = 4; W32(bootp + o, offeredIp_); o += 4;
    bootp[o++] = 54; bootp[o++] = 4; W32(bootp + o, offerServer_); o += 4;
    bootp[o++] = 61; bootp[o++] = 7; bootp[o++] = 1; memcpy(bootp + o, cfg_.mac, 6); o += 6;
    bootp[o++] = 55; bootp[o++] = 6; bootp[o++] = 1; bootp[o++] = 3; bootp[o++] = 6;
    bootp[o++] = 51; bootp[o++] = 54; bootp[o++] = 58;
    bootp[o++] = 255;
    BYTE bcast[6]; Broadcast(bcast); dhcpRequested_ = true;
    return SendIpv4Udp(0xFFFFFFFFu, 68, 67, bootp, o, bcast);
}


bool StandaloneNetStack::SendDhcpRenew() {
    if (!configured_ || !gatewayMacValid_ || !cfg_.dhcpServer) return false;
    xid_ = (xid_ ^ leaseAgeSeconds_ ^ 0x52454E45u) & 0xFFFFFFFFu;
    BYTE bootp[548]; memset(bootp, 0, sizeof(bootp));
    bootp[0] = 1; bootp[1] = 1; bootp[2] = 6; W32(bootp + 4, xid_);
    W32(bootp + 12, cfg_.ip); memcpy(bootp + 28, cfg_.mac, 6);
    bootp[236] = 99; bootp[237] = 130; bootp[238] = 83; bootp[239] = 99;
    unsigned o = 240;
    bootp[o++] = 53; bootp[o++] = 1; bootp[o++] = 3;
    bootp[o++] = 61; bootp[o++] = 7; bootp[o++] = 1; memcpy(bootp + o, cfg_.mac, 6); o += 6;
    bootp[o++] = 55; bootp[o++] = 5; bootp[o++] = 1; bootp[o++] = 3; bootp[o++] = 6; bootp[o++] = 51; bootp[o++] = 54;
    bootp[o++] = 255; renewing_ = true; dhcpSeconds_ = 0;
    return SendIpv4Udp(cfg_.dhcpServer, 68, 67, bootp, o, gatewayMac_);
}

void StandaloneNetStack::TickOneSecond() {
    if (!send_) return;
    if (!configured_) {
        if (++dhcpSeconds_ >= 4) {
            dhcpSeconds_ = 0;
            if (dhcpRequested_ && offeredIp_) SendDhcpRequest();
            else SendDhcpDiscover();
        }
        return;
    }
    if (!cfg_.leaseSeconds) return;
    ++leaseAgeSeconds_;
    DWORD t1 = cfg_.leaseSeconds / 2; if (!t1) t1 = 1;
    if (leaseAgeSeconds_ == t1) SendDhcpRenew();
    if (leaseAgeSeconds_ >= cfg_.leaseSeconds) {
        configured_ = false; gatewayMacValid_ = false; renewing_ = false;
        offeredIp_ = 0; offerServer_ = 0; memset(&cfg_.ip, 0, sizeof(DWORD) * 6);
        SendDhcpDiscover();
    }
}

bool StandaloneNetStack::SendIpv4Udp(DWORD dstIp, WORD srcPort, WORD dstPort,
                                     const BYTE* payload, unsigned payloadLen, const BYTE dstMac[6]) {
    if (payloadLen > 1400) return false;
    BYTE udp[8 + 1400];
    W16(udp, srcPort); W16(udp + 2, dstPort); W16(udp + 4, (WORD)(8 + payloadLen)); W16(udp + 6, 0);
    if (payloadLen) memcpy(udp + 8, payload, payloadLen);
    const DWORD srcIp = configured_ ? cfg_.ip : 0;
    W16(udp + 6, TransportChecksum(srcIp, dstIp, 17, udp, 8 + payloadLen));
    return SendIpv4Raw(dstIp, 17, udp, 8 + payloadLen, dstMac);
}

bool StandaloneNetStack::SendIpv4Raw(DWORD dstIp, BYTE protocol, const BYTE* payload,
                                     unsigned payloadLen, const BYTE dstMac[6]) {
    if (!dstMac || payloadLen > 1480) return false;
    BYTE frame[1514];
    memcpy(frame, dstMac, 6); memcpy(frame + 6, cfg_.mac, 6); W16(frame + 12, 0x0800);
    BYTE* ip = frame + 14; memset(ip, 0, 20);
    ip[0] = 0x45; W16(ip + 2, (WORD)(20 + payloadLen)); W16(ip + 4, ipId_++);
    W16(ip + 6, 0x4000); ip[8] = 64; ip[9] = protocol;
    W32(ip + 12, configured_ ? cfg_.ip : 0); W32(ip + 16, dstIp); W16(ip + 10, Checksum(ip, 20));
    if (payloadLen) memcpy(ip + 20, payload, payloadLen);
    return SendFrame(frame, 14 + 20 + payloadLen);
}

bool StandaloneNetStack::OnEthernetFrame(const BYTE* frame, unsigned length) {
    if (!frame || length < 14) return false;
    if (!MacEq(frame, cfg_.mac) && !IsBroadcastMac(frame) && !(frame[0] & 1)) return true;
    const WORD type = R16(frame + 12);
    if (type == 0x0806) return HandleArp(frame + 14, length - 14);
    if (type == 0x0800) return HandleIpv4(frame + 14, length - 14);
    return true;
}

bool StandaloneNetStack::HandleArp(const BYTE* p, unsigned n) {
    if (n < 28 || R16(p) != 1 || R16(p + 2) != 0x0800 || p[4] != 6 || p[5] != 4) return true;
    const WORD op = R16(p + 6); const BYTE* senderMac = p + 8; const DWORD senderIp = R32(p + 14); const DWORD targetIp = R32(p + 24);
    if (op == 2 && configured_ && senderIp == cfg_.gateway) {
        memcpy(gatewayMac_, senderMac, 6); gatewayMacValid_ = true;
        Event(NetEventGatewayResolved, senderIp);
        SendPing(cfg_.gateway, (WORD)(dnsId_ ^ 0x1111), 1);
        return SendDnsQuery();
    }
    if (op == 1 && configured_ && targetIp == cfg_.ip) {
        BYTE frame[42]; memcpy(frame, senderMac, 6); memcpy(frame + 6, cfg_.mac, 6); W16(frame + 12, 0x0806);
        BYTE* a = frame + 14; W16(a, 1); W16(a + 2, 0x0800); a[4] = 6; a[5] = 4; W16(a + 6, 2);
        memcpy(a + 8, cfg_.mac, 6); W32(a + 14, cfg_.ip); memcpy(a + 18, senderMac, 6); W32(a + 24, senderIp);
        return SendFrame(frame, sizeof(frame));
    }
    return true;
}

bool StandaloneNetStack::HandleIpv4(const BYTE* p, unsigned n) {
    if (n < 20 || (p[0] >> 4) != 4) return true;
    const unsigned ihl = (p[0] & 0x0F) * 4; if (ihl < 20 || n < ihl) return true;
    const unsigned total = R16(p + 2); if (total < ihl || total > n) return true;
    if ((R16(p + 6) & 0x3FFFu) != 0) return true; // reject MF/fragment offsets; DF is allowed
    const DWORD src = R32(p + 12); const DWORD dst = R32(p + 16);
    if (configured_ && dst != cfg_.ip && dst != 0xFFFFFFFFu) return true;
    const BYTE* body = p + ihl; const unsigned bodyLen = total - ihl;
    if (p[9] == 17) return HandleUdp(src, dst, body, bodyLen);
    if (p[9] == 1) return HandleIcmp(src, body, bodyLen);
    if (p[9] == 6) return HandleTcp(src, body, bodyLen);
    return true;
}

bool StandaloneNetStack::HandleUdp(DWORD srcIp, DWORD, const BYTE* p, unsigned n) {
    if (n < 8) return true;
    const WORD sp = R16(p);
    const WORD dp = R16(p + 2);
    const unsigned len = R16(p + 4);
    if (len < 8 || len > n) return true;
    if (sp == 67 && dp == 68) return HandleDhcp(p + 8, len - 8);
    if (sp == 53 && dp == (WORD)(53000u + (dnsId_ & 0x03FFu))) return HandleDns(p + 8, len - 8);
    (void)srcIp; return true;
}

bool StandaloneNetStack::HandleDhcp(const BYTE* p, unsigned n) {
    if (n < 241 || p[0] != 2 || p[1] != 1 || p[2] != 6 || R32(p + 4) != xid_) return true;
    if (p[236] != 99 || p[237] != 130 || p[238] != 83 || p[239] != 99) return true;
    const DWORD yiaddr = R32(p + 16);
    BYTE msg = 0; DWORD subnet = 0, router = 0, dns = 0, server = 0, lease = 0;
    unsigned o = 240;
    while (o < n) {
        BYTE code = p[o++]; if (code == 255) break; if (code == 0) continue; if (o >= n) break;
        BYTE len = p[o++]; if (o + len > n) break;
        if (code == 53 && len >= 1) msg = p[o];
        else if (code == 1 && len >= 4) subnet = R32(p + o);
        else if (code == 3 && len >= 4) router = R32(p + o);
        else if (code == 6 && len >= 4) dns = R32(p + o);
        else if (code == 54 && len >= 4) server = R32(p + o);
        else if (code == 51 && len >= 4) lease = R32(p + o);
        o += len;
    }
    if (msg == 2 && yiaddr) {
        dhcpSeconds_ = 0;
        offeredIp_ = yiaddr; offerServer_ = server ? server : R32(p + 20);
        cfg_.subnet = subnet; cfg_.gateway = router; cfg_.dns = dns; cfg_.leaseSeconds = lease;
        Event(NetEventDhcpOffer, yiaddr); return SendDhcpRequest();
    }
    if (msg == 5) {
        DWORD assigned = yiaddr ? yiaddr : (renewing_ ? cfg_.ip : 0);
        if (!assigned || (!renewing_ && offeredIp_ && assigned != offeredIp_)) return true;
        cfg_.ip = assigned; if (subnet) cfg_.subnet = subnet; if (router) cfg_.gateway = router;
        if (dns) cfg_.dns = dns;
        cfg_.dhcpServer = server ? server : offerServer_;
        if (lease) cfg_.leaseSeconds = lease;
        if (!cfg_.subnet) cfg_.subnet = 0xFFFFFF00u;
        if (!cfg_.gateway) cfg_.gateway = cfg_.dhcpServer;
        if (!cfg_.dns) cfg_.dns = cfg_.gateway;
        configured_ = cfg_.ip && cfg_.gateway && cfg_.dns;
        renewing_ = false; dhcpSeconds_ = 0; leaseAgeSeconds_ = 0;
        if (!configured_) { Event(NetEventFailure, 0x44484350u); return false; }
        Event(NetEventDhcpBound, cfg_.ip); return SendArpRequest(cfg_.gateway);
    }
    if (msg == 6) { Event(NetEventFailure, 0x4E414B00u); return false; }
    return true;
}

bool StandaloneNetStack::SendArpRequest(DWORD target) {
    BYTE frame[42]; BYTE bcast[6]; Broadcast(bcast);
    memcpy(frame, bcast, 6); memcpy(frame + 6, cfg_.mac, 6); W16(frame + 12, 0x0806);
    BYTE* a = frame + 14; W16(a, 1); W16(a + 2, 0x0800); a[4] = 6; a[5] = 4; W16(a + 6, 1);
    memcpy(a + 8, cfg_.mac, 6); W32(a + 14, cfg_.ip); memset(a + 18, 0, 6); W32(a + 24, target);
    return SendFrame(frame, sizeof(frame));
}

bool StandaloneNetStack::SendPing(DWORD destination, WORD identifier, WORD sequence) {
    if (!configured_ || !gatewayMacValid_) return false;
    BYTE icmp[16]; memset(icmp, 0, sizeof(icmp)); icmp[0] = 8; W16(icmp + 4, identifier); W16(icmp + 6, sequence);
    W32(icmp + 8, seed_); W32(icmp + 12, destination ^ seed_); W16(icmp + 2, Checksum(icmp, sizeof(icmp)));
    return SendIpv4Raw(destination, 1, icmp, sizeof(icmp), gatewayMac_);
}

bool StandaloneNetStack::HandleIcmp(DWORD srcIp, const BYTE* p, unsigned n) {
    if (n >= 8 && p[0] == 0) Event(NetEventIcmpReply, srcIp);
    return true;
}

bool StandaloneNetStack::SendDnsQuery() {
    if (!configured_ || !gatewayMacValid_) return false;
    BYTE dns[256]; memset(dns, 0, sizeof(dns)); W16(dns, dnsId_); W16(dns + 2, 0x0100); W16(dns + 4, 1);
    unsigned o = 12; const char* labels[2] = {"example", "com"};
    for (unsigned l = 0; l < 2; ++l) { unsigned len = (unsigned)strlen(labels[l]); dns[o++] = (BYTE)len; memcpy(dns + o, labels[l], len); o += len; }
    dns[o++] = 0; W16(dns + o, 1); o += 2; W16(dns + o, 1); o += 2;
    const WORD port = (WORD)(53000u + (dnsId_ & 0x03FFu));
    return SendIpv4Udp(cfg_.dns, port, 53, dns, o, gatewayMac_);
}

bool StandaloneNetStack::HandleDns(const BYTE* p, unsigned n) {
    if (n < 12 || R16(p) != dnsId_ || (R16(p + 2) & 0x8000) == 0) return true;
    const unsigned qd = R16(p + 4), an = R16(p + 6); unsigned o = 12;
    for (unsigned i = 0; i < qd; ++i) { o = SkipDnsName(p, n, o); if (!o || o + 4 > n) return true; o += 4; }
    for (unsigned i = 0; i < an; ++i) {
        o = SkipDnsName(p, n, o); if (!o || o + 10 > n) return true;
        WORD type = R16(p + o); WORD klass = R16(p + o + 2); WORD rdlen = R16(p + o + 8); o += 10;
        if (o + rdlen > n) return true;
        if (type == 1 && klass == 1 && rdlen == 4) {
            resolvedIp_ = R32(p + o); Event(NetEventDnsResolved, resolvedIp_); return SendTcpSyn();
        }
        o += rdlen;
    }
    Event(NetEventFailure, 0x444E5300u); return false;
}

bool StandaloneNetStack::SendTcpSyn() {
    if (!resolvedIp_ || !gatewayMacValid_) return false;
    tcpSynSent_ = true;
    if (!SendTcpAck(0x02, 0, 0)) return false;
    tcpSeq_ = (tcpSeq_ + 1) & 0xFFFFFFFFu;
    return true;
}

bool StandaloneNetStack::SendTcpAck(BYTE flags, const BYTE* payload, unsigned payloadLen) {
    if (!resolvedIp_ || !gatewayMacValid_ || payloadLen > 1400) return false;
    BYTE tcp[20 + 1400]; memset(tcp, 0, 20);
    W16(tcp, tcpLocalPort_); W16(tcp + 2, 80); W32(tcp + 4, tcpSeq_); W32(tcp + 8, tcpAck_);
    tcp[12] = 5u << 4; tcp[13] = flags; W16(tcp + 14, 32768);
    if (payloadLen) memcpy(tcp + 20, payload, payloadLen);
    W16(tcp + 16, TransportChecksum(cfg_.ip, resolvedIp_, 6, tcp, 20 + payloadLen));
    return SendIpv4Raw(resolvedIp_, 6, tcp, 20 + payloadLen, gatewayMac_);
}

bool StandaloneNetStack::HandleTcp(DWORD srcIp, const BYTE* p, unsigned n) {
    if (srcIp != resolvedIp_ || n < 20 || R16(p) != 80 || R16(p + 2) != tcpLocalPort_) return true;
    const unsigned hlen = (p[12] >> 4) * 4; if (hlen < 20 || hlen > n) return true;
    const BYTE flags = p[13]; const DWORD seq = R32(p + 4); const DWORD ack = R32(p + 8);
    const BYTE* data = p + hlen; const unsigned dataLen = n - hlen;
    if (tcpSynSent_ && !tcpEstablished_ && (flags & 0x12) == 0x12 && ack == tcpSeq_) {
        tcpAck_ = (seq + 1) & 0xFFFFFFFFu; tcpEstablished_ = true; Event(NetEventTcpConnected, srcIp);
        if (!SendTcpAck(0x10, 0, 0)) return false;
        static const char req[] = "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\nUser-Agent: iPhoneTether360/9C\r\n\r\n";
        const unsigned reqLen = (unsigned)strlen(req);
        if (!SendTcpAck(0x18, (const BYTE*)req, reqLen)) return false;
        tcpSeq_ = (tcpSeq_ + reqLen) & 0xFFFFFFFFu; httpSent_ = true;
        return true;
    }
    if (!tcpEstablished_) return true;
    if (dataLen && seq == tcpAck_) {
        tcpAck_ = (tcpAck_ + dataLen) & 0xFFFFFFFFu;
        if (ContainsHttp(data, dataLen) && !internetOk_) { internetOk_ = true; Event(NetEventHttpResponse, srcIp); }
    }
    if ((flags & 0x01) && seq + dataLen == tcpAck_) tcpAck_ = (tcpAck_ + 1) & 0xFFFFFFFFu;
    if (dataLen || (flags & 0x01)) SendTcpAck(0x10, 0, 0);
    return true;
}

const BYTE* StandaloneNetStack::RouteMac(DWORD) const { return gatewayMacValid_ ? gatewayMac_ : 0; }

} // namespace it360_net
