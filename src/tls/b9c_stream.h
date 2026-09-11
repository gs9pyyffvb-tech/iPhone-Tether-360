#pragma once
#include "net_stack.h"
#include "tls/deadline.h"
namespace it360_tls {
class B9cTcpStream {
public:
    B9cTcpStream();
    void Reset();
    bool Configure(const it360_net::NetConfig& config, const BYTE gateway_mac[6],
                   it360_net::FrameSendFn send, void* send_user);
    bool ResolveHost(const char* hostname, DWORD* address, const Deadline& deadline);
    bool Connect(DWORD address, WORD port, const Deadline& deadline);
    int Read(void* data, unsigned capacity, const Deadline& deadline);
    int Write(const void* data, unsigned length, const Deadline& deadline);
    bool OnEthernetFrame(const BYTE* frame, unsigned length);
    void Close();
    bool Connected() const;
private:
    bool SendDnsQuery(const char* hostname);
    bool SendTcp(BYTE flags, const BYTE* payload, unsigned length, DWORD sequence);
    bool SendIpv4Udp(DWORD dst_ip, WORD src_port, WORD dst_port,
                     const BYTE* payload, unsigned length);
    bool SendIpv4Raw(DWORD dst_ip, BYTE protocol, const BYTE* payload, unsigned length);
    void Fail();
    bool PushRx(const BYTE* data, unsigned length);
    unsigned PopRx(BYTE* data, unsigned capacity);

    it360_net::NetConfig cfg_;
    BYTE gateway_mac_[6];
    it360_net::FrameSendFn send_;
    void* send_user_;
    DWORD remote_ip_;
    WORD remote_port_;
    WORD local_port_;
    WORD dns_id_;
    WORD ip_id_;
    DWORD send_seq_;
    DWORD recv_seq_;
    volatile DWORD peer_ack_;
    volatile DWORD resolved_ip_;
    volatile LONG configured_;
    volatile LONG dns_done_;
    volatile LONG connected_;
    volatile LONG closed_;
    volatile LONG failed_;
    volatile LONG rx_lock_;
    BYTE rx_[16384];
    unsigned rx_head_, rx_tail_, rx_count_;
};
}
