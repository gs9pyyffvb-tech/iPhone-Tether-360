#include "pair_session.h"
#include "base64.h"
#include <string.h>

namespace it360 {

static void WriteBe32(uint8_t* p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}

static bool Append(char*& p, size_t& left, const char* s) {
    if (!s) return false;
    const size_t n = strlen(s);
    if (n + 1 > left) return false;
    memcpy(p, s, n);
    p += n;
    left -= n;
    *p = 0;
    return true;
}

size_t BuildGetValueFrame(const char* key, uint8_t* out, size_t cap) {
    if (!key || !*key || !out || cap < 5) return 0;
    char* p = (char*)out + 4;
    size_t left = cap - 4;
    *p = 0;
    if (!Append(p,left,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                       "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                       "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"
                       "<plist version=\"1.0\"><dict>"
                       "<key>Label</key><string>iPhoneTether360</string>"
                       "<key>Request</key><string>GetValue</string>"
                       "<key>Key</key><string>")) return 0;
    if (!Append(p,left,key)) return 0;
    if (!Append(p,left,"</string></dict></plist>")) return 0;
    const size_t n = (size_t)(p - ((char*)out + 4));
    WriteBe32(out, (uint32_t)n);
    return n + 4;
}

size_t BuildGetDevicePublicKeyFrame(uint8_t* out, size_t cap) {
    return BuildGetValueFrame("DevicePublicKey", out, cap);
}

size_t BuildGetUniqueDeviceIdFrame(uint8_t* out, size_t cap) {
    return BuildGetValueFrame("UniqueDeviceID", out, cap);
}

size_t BuildGetWiFiAddressFrame(uint8_t* out, size_t cap) {
    return BuildGetValueFrame("WiFiAddress", out, cap);
}

bool ParseGetValueStringXml(const char* xml, char* dst, size_t cap) {
    return XmlExtractStringValue(xml, "Value", dst, cap);
}

size_t ParseDevicePublicKeyXml(const char* xml, uint8_t* dst, size_t cap) {
    if (!xml || !dst || cap < 2) return 0;
    char encoded[6144];
    if (!XmlExtractDataB64Value(xml, "Value", encoded, sizeof(encoded))) return 0;
    size_t n = 0;
    n = b64::Decode(encoded, strlen(encoded), dst, cap - 1);
    if (!n) return 0;
    dst[n] = 0;
    return n;
}

static size_t BuildPairingFrame(const PairIdentity& i, bool validate, uint8_t* out, size_t cap) {
    if (!out || cap < 5) return 0;
    PairMaterialView m;
    m.host_id = i.host_id;
    m.system_buid = i.system_buid;
    m.host_certificate_b64 = i.host_certificate_b64;
    m.device_certificate_b64 = i.device_certificate_b64;
    m.root_certificate_b64 = i.root_certificate_b64;
    const size_t n = validate
        ? BuildValidatePairRequestXml(m, (char*)out + 4, cap - 4)
        : BuildPairRequestXml(m, (char*)out + 4, cap - 4);
    if (!n) return 0;
    WriteBe32(out,(uint32_t)n);
    return n + 4;
}

size_t BuildPairFrame(const PairIdentity& i, uint8_t* out, size_t cap) {
    return BuildPairingFrame(i, false, out, cap);
}

size_t BuildValidatePairFrame(const PairIdentity& i, uint8_t* out, size_t cap) {
    return BuildPairingFrame(i, true, out, cap);
}

PairSession::PairSession() { Reset(); }

void PairSession::Reset() {
    state_ = PAIR_SESSION_IDLE;
    memset(&identity_, 0, sizeof(identity_));
    memset(device_public_key_, 0, sizeof(device_public_key_));
}

size_t PairSession::Begin(uint8_t* frame, size_t cap) {
    if (state_ != PAIR_SESSION_IDLE) return 0;
    const size_t n = BuildGetDevicePublicKeyFrame(frame, cap);
    if (n) state_ = PAIR_SESSION_GET_VALUE_SENT;
    return n;
}

size_t PairSession::OnDevicePublicKeyReply(const char* xml,const PairCryptoBackend& crypto,
                                            uint8_t* frame,size_t cap) {
    if (state_ != PAIR_SESSION_GET_VALUE_SENT) return 0;
    const size_t key_len = ParseDevicePublicKeyXml(xml, device_public_key_, sizeof(device_public_key_));
    if (!key_len || !GeneratePairIdentity(crypto, device_public_key_, key_len, identity_)) {
        state_ = PAIR_SESSION_FAILED;
        return 0;
    }
    const size_t n = BuildPairFrame(identity_, frame, cap);
    if (!n) {
        state_ = PAIR_SESSION_FAILED;
        return 0;
    }
    state_ = PAIR_SESSION_PAIR_SENT;
    return n;
}

PairReply PairSession::OnPairReply(const char* xml) {
    if (state_ != PAIR_SESSION_PAIR_SENT && state_ != PAIR_SESSION_PENDING_TRUST)
        return PAIR_REPLY_INVALID;
    const PairReply r = ParsePairReplyXml(xml);
    if (r == PAIR_REPLY_SUCCESS) state_ = PAIR_SESSION_PAIRED;
    else if (r == PAIR_REPLY_PENDING_TRUST) state_ = PAIR_SESSION_PENDING_TRUST;
    else if (r == PAIR_REPLY_DENIED) state_ = PAIR_SESSION_DENIED;
    else if (r == PAIR_REPLY_ERROR || r == PAIR_REPLY_INVALID || r == PAIR_REPLY_INVALID_HOST)
        state_ = PAIR_SESSION_FAILED;
    return r;
}

} // namespace it360
