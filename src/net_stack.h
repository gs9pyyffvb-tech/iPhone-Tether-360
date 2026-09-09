#pragma once
#include <stddef.h>
#include "platform/xbox_platform.h"

namespace it360_net {

struct NetConfig {
    BYTE mac[6];
    DWORD ip;
    DWORD subnet;
    DWORD gateway;
    DWORD dns;
    DWORD dhcpServer;
    DWORD leaseSeconds;
};

enum NetEvent {
    NetEventDhcpDiscover,
    NetEventDhcpOffer,
    NetEventDhcpBound,
    NetEventGatewayResolved,
    NetEventIcmpReply,
    NetEventDnsResolved,
    NetEventTcpConnected,
    NetEventHttpResponse,
    NetEventFailure
};

typedef bool (*FrameSendFn)(const BYTE* frame, unsigned length, void* user);
typedef void (*NetEventFn)(NetEvent event, const NetConfig* cfg, DWORD detail, void* user);

class StandaloneNetStack {
public:
    StandaloneNetStack();
    void Reset();
    bool Start(const BYTE mac[6], DWORD seed, FrameSendFn send, NetEventFn event, void* user);
    bool OnEthernetFrame(const BYTE* frame, unsigned length);
    void TickOneSecond();
    bool SendPing(DWORD destination, WORD identifier, WORD sequence);
    const NetConfig& Config() const { return cfg_; }
    bool IsConfigured() const { return configured_; }
    bool InternetValidated() const { return internetOk_; }
    DWORD ResolvedAddress() const { return resolvedIp_; }

private:
    bool SendFrame(const BYTE* frame, unsigned len);
    void Event(NetEvent e, DWORD detail);
    bool SendDhcpDiscover();
    bool SendDhcpRequest();
    bool SendDhcpRenew();
    bool HandleArp(const BYTE* p, unsigned n);
    bool HandleIpv4(const BYTE* p, unsigned n);
    bool HandleUdp(DWORD srcIp, DWORD dstIp, const BYTE* p, unsigned n);
    bool HandleDhcp(const BYTE* p, unsigned n);
    bool HandleDns(const BYTE* p, unsigned n);
    bool HandleIcmp(DWORD srcIp, const BYTE* p, unsigned n);
    bool HandleTcp(DWORD srcIp, const BYTE* p, unsigned n);
    bool SendArpRequest(DWORD target);
    bool SendDnsQuery();
    bool SendTcpSyn();
    bool SendTcpAck(BYTE flags, const BYTE* payload, unsigned payloadLen);
    bool SendIpv4Udp(DWORD dstIp, WORD srcPort, WORD dstPort,
                     const BYTE* payload, unsigned payloadLen, const BYTE dstMac[6]);
    bool SendIpv4Raw(DWORD dstIp, BYTE protocol, const BYTE* payload,
                     unsigned payloadLen, const BYTE dstMac[6]);
    const BYTE* RouteMac(DWORD dstIp) const;

    NetConfig cfg_;
    FrameSendFn send_;
    NetEventFn event_;
    void* user_;
    DWORD seed_;
    DWORD xid_;
    DWORD offeredIp_;
    DWORD offerServer_;
    DWORD resolvedIp_;
    BYTE gatewayMac_[6];
    bool gatewayMacValid_;
    bool configured_;
    bool internetOk_;
    bool dhcpRequested_;
    bool renewing_;
    unsigned dhcpSeconds_;
    DWORD leaseAgeSeconds_;
    WORD dnsId_;
    WORD ipId_;
    WORD tcpLocalPort_;
    DWORD tcpSeq_;
    DWORD tcpAck_;
    bool tcpSynSent_;
    bool tcpEstablished_;
    bool httpSent_;
};

} // namespace it360_net
