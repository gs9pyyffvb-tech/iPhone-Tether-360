#pragma once
#include <stddef.h>
#include <stdint.h>

namespace it360 {

// Values ending in _b64 are base64(PEM bytes), ready for a plist <data> node.
struct PairMaterialView {
    const char* host_id;
    const char* system_buid;
    const char* host_certificate_b64;
    const char* device_certificate_b64;
    const char* root_certificate_b64;
};

size_t XmlEscape(const char* src, char* dst, size_t dst_cap);
bool XmlExtractStringValue(const char* xml, const char* key, char* dst, size_t cap);
bool XmlExtractDataB64Value(const char* xml, const char* key, char* dst, size_t cap);

size_t BuildPairingRequestXml(const char* request, const PairMaterialView& m,
                              bool extended_pairing_errors, char* dst, size_t dst_cap);
size_t BuildPairRequestXml(const PairMaterialView& m, char* dst, size_t dst_cap);
size_t BuildValidatePairRequestXml(const PairMaterialView& m, char* dst, size_t dst_cap);

enum PairReply {
    PAIR_REPLY_INVALID = 0,
    PAIR_REPLY_SUCCESS,
    PAIR_REPLY_PENDING_TRUST,
    PAIR_REPLY_DENIED,
    PAIR_REPLY_INVALID_HOST,
    PAIR_REPLY_ERROR
};

PairReply ParsePairingReplyXml(const char* xml, const char* expected_request);
PairReply ParsePairReplyXml(const char* xml);
PairReply ParseValidatePairReplyXml(const char* xml);

} // namespace it360
