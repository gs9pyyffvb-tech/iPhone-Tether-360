#include "mux_proto.h"
#include <string.h>

namespace iphone_mux {

uint16_t ReadBe16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

uint32_t ReadBe32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void WriteBe16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

void WriteBe32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

size_t BuildVersionRequest(uint8_t* out, size_t capacity) {
    // Initial usbmux version negotiation uses the legacy 8-byte mux header.
    const size_t total = 8 + 12;
    if (!out || capacity < total) return 0;
    memset(out, 0, total);
    WriteBe32(out + 0, kProtoVersion);
    WriteBe32(out + 4, (uint32_t)total);
    WriteBe32(out + 8, 2); // major
    WriteBe32(out + 12, 0); // minor
    WriteBe32(out + 16, 0); // padding
    return total;
}

static size_t BuildMuxV2Header(uint8_t* out, size_t capacity,
                               uint32_t protocol, uint32_t total,
                               uint16_t txSeq, uint16_t rxSeq) {
    if (!out || capacity < 16) return 0;
    WriteBe32(out + 0, protocol);
    WriteBe32(out + 4, total);
    WriteBe32(out + 8, kMuxMagic);
    WriteBe16(out + 12, txSeq);
    WriteBe16(out + 14, rxSeq);
    return 16;
}

size_t BuildSetupPacket(uint8_t* out, size_t capacity, uint16_t txSeq, uint16_t rxSeq) {
    const size_t total = 17;
    if (!out || capacity < total) return 0;
    if (!BuildMuxV2Header(out, capacity, kProtoSetup, (uint32_t)total, txSeq, rxSeq)) return 0;
    out[16] = 0x07;
    return total;
}

size_t BuildTcpPacket(uint8_t* out, size_t capacity,
                      uint16_t muxTxSeq, uint16_t muxRxSeq,
                      uint16_t sourcePort, uint16_t destPort,
                      uint32_t sequence, uint32_t acknowledgement,
                      uint8_t flags, uint32_t receiveWindow,
                      const uint8_t* payload, size_t payloadLength) {
    const size_t tcpHeader = 20;
    const size_t total = 16 + tcpHeader + payloadLength;
    if (!out || capacity < total || (payloadLength && !payload)) return 0;
    memset(out, 0, total);
    if (!BuildMuxV2Header(out, capacity, kProtoTcp, (uint32_t)total, muxTxSeq, muxRxSeq)) return 0;

    uint8_t* tcp = out + 16;
    WriteBe16(tcp + 0, sourcePort);
    WriteBe16(tcp + 2, destPort);
    WriteBe32(tcp + 4, sequence);
    WriteBe32(tcp + 8, acknowledgement);
    tcp[12] = 5u << 4; // 20-byte header
    tcp[13] = flags;
    uint32_t scaled = receiveWindow >> 8;
    if (scaled > 0xFFFFu) scaled = 0xFFFFu;
    WriteBe16(tcp + 14, (uint16_t)scaled);
    // checksum + urgent pointer remain zero, matching usbmuxd's synthetic TCP framing.
    if (payloadLength) memcpy(out + 36, payload, payloadLength);
    return total;
}

bool ParseMux(const uint8_t* data, size_t available, bool v2, ParsedMux* out) {
    if (!data || !out) return false;
    const unsigned header = v2 ? 16u : 8u;
    if (available < header) return false;
    const uint32_t total = ReadBe32(data + 4);
    if (total < header || total > available) return false;
    if (v2 && ReadBe32(data + 8) != kMuxMagic) return false;

    out->protocol = ReadBe32(data + 0);
    out->totalLength = total;
    out->txSeq = v2 ? ReadBe16(data + 12) : 0;
    out->rxSeq = v2 ? ReadBe16(data + 14) : 0;
    out->headerLength = header;
    out->payload = data + header;
    out->payloadLength = (unsigned)(total - header);
    return true;
}

bool ParseTcp(const ParsedMux& mux, ParsedTcp* out) {
    if (!out || mux.protocol != kProtoTcp || mux.payloadLength < 20) return false;
    const uint8_t* tcp = mux.payload;
    const unsigned headerWords = (unsigned)(tcp[12] >> 4);
    const unsigned tcpHeader = headerWords * 4u;
    if (headerWords < 5 || tcpHeader > mux.payloadLength) return false;

    out->sourcePort = ReadBe16(tcp + 0);
    out->destPort = ReadBe16(tcp + 2);
    out->sequence = ReadBe32(tcp + 4);
    out->acknowledgement = ReadBe32(tcp + 8);
    out->flags = tcp[13];
    out->windowEncoded = ReadBe16(tcp + 14);
    out->payload = tcp + tcpHeader;
    out->payloadLength = mux.payloadLength - tcpHeader;
    return true;
}

} // namespace iphone_mux
