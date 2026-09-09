#pragma once
#include <stddef.h>
#include <stdint.h>

namespace iphone_mux {

static const uint32_t kProtoVersion = 0;
static const uint32_t kProtoControl = 1;
static const uint32_t kProtoSetup   = 2;
static const uint32_t kProtoTcp     = 6;
static const uint32_t kMuxMagic     = 0xFEEDFACEu;

static const uint8_t kTcpFin = 0x01;
static const uint8_t kTcpSyn = 0x02;
static const uint8_t kTcpRst = 0x04;
static const uint8_t kTcpAck = 0x10;

struct ParsedMux {
    uint32_t protocol;
    uint32_t totalLength;
    uint16_t txSeq;
    uint16_t rxSeq;
    unsigned headerLength;
    const uint8_t* payload;
    unsigned payloadLength;
};

struct ParsedTcp {
    uint16_t sourcePort;
    uint16_t destPort;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint8_t flags;
    uint16_t windowEncoded;
    const uint8_t* payload;
    unsigned payloadLength;
};

uint16_t ReadBe16(const uint8_t* p);
uint32_t ReadBe32(const uint8_t* p);
void WriteBe16(uint8_t* p, uint16_t v);
void WriteBe32(uint8_t* p, uint32_t v);

size_t BuildVersionRequest(uint8_t* out, size_t capacity);
size_t BuildSetupPacket(uint8_t* out, size_t capacity, uint16_t txSeq, uint16_t rxSeq);
size_t BuildTcpPacket(uint8_t* out, size_t capacity,
                      uint16_t muxTxSeq, uint16_t muxRxSeq,
                      uint16_t sourcePort, uint16_t destPort,
                      uint32_t sequence, uint32_t acknowledgement,
                      uint8_t flags, uint32_t receiveWindow,
                      const uint8_t* payload, size_t payloadLength);

bool ParseMux(const uint8_t* data, size_t available, bool v2, ParsedMux* out);
bool ParseTcp(const ParsedMux& mux, ParsedTcp* out);

} // namespace iphone_mux
