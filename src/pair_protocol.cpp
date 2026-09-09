#include "pair_protocol.h"
#include <string.h>

namespace it360 {

static bool append(char*& p, size_t& left, const char* s) {
    if (!s) return false;
    const size_t n = strlen(s);
    if (n + 1 > left) return false;
    memcpy(p, s, n);
    p += n;
    left -= n;
    *p = 0;
    return true;
}

size_t XmlEscape(const char* src, char* dst, size_t dst_cap) {
    if (!src || !dst || !dst_cap) return 0;
    char* p = dst;
    size_t left = dst_cap;
    *p = 0;
    for (; *src; ++src) {
        const char* repl = 0;
        switch (*src) {
            case '&': repl = "&amp;"; break;
            case '<': repl = "&lt;"; break;
            case '>': repl = "&gt;"; break;
            case '"': repl = "&quot;"; break;
            case '\'': repl = "&apos;"; break;
            default: break;
        }
        if (repl) {
            if (!append(p, left, repl)) return 0;
        } else {
            if (left < 2) return 0;
            *p++ = *src;
            *p = 0;
            --left;
        }
    }
    return (size_t)(p - dst);
}

static const char* find_key(const char* xml, const char* key) {
    if (!xml || !key || !*key) return 0;
    char needle[160];
    const size_t kn = strlen(key);
    if (kn + 12 >= sizeof(needle)) return 0;
    memcpy(needle, "<key>", 5);
    memcpy(needle + 5, key, kn);
    memcpy(needle + 5 + kn, "</key>", 7);
    needle[12 + kn] = 0;
    return strstr(xml, needle);
}

static bool xml_unescape(const char* b, const char* e, char* dst, size_t cap) {
    if (!b || !e || e < b || !dst || !cap) return false;
    size_t o = 0;
    for (const char* p = b; p < e;) {
        char c = *p++;
        if (c == '&') {
            const size_t remain = (size_t)(e - (p - 1));
            if (remain >= 5 && !memcmp(p - 1, "&amp;", 5)) { c='&'; p += 4; }
            else if (remain >= 4 && !memcmp(p - 1, "&lt;", 4)) { c='<'; p += 3; }
            else if (remain >= 4 && !memcmp(p - 1, "&gt;", 4)) { c='>'; p += 3; }
            else if (remain >= 6 && !memcmp(p - 1, "&quot;", 6)) { c='"'; p += 5; }
            else if (remain >= 6 && !memcmp(p - 1, "&apos;", 6)) { c='\''; p += 5; }
            else return false;
        }
        if (o + 1 >= cap) return false;
        dst[o++] = c;
    }
    dst[o] = 0;
    return true;
}

bool XmlExtractStringValue(const char* xml, const char* key, char* dst, size_t cap) {
    const char* k = find_key(xml, key);
    if (!k || !dst || cap < 2) return false;
    const char* b = strstr(k, "<string>");
    if (!b) return false;
    b += 8;
    const char* e = strstr(b, "</string>");
    if (!e) return false;
    return xml_unescape(b, e, dst, cap);
}

bool XmlExtractDataB64Value(const char* xml, const char* key, char* dst, size_t cap) {
    const char* k = find_key(xml, key);
    if (!k || !dst || cap < 2) return false;
    const char* b = strstr(k, "<data>");
    if (!b) return false;
    b += 6;
    const char* e = strstr(b, "</data>");
    if (!e) return false;
    size_t o = 0;
    for (const char* p = b; p < e; ++p) {
        const char c = *p;
        if (c==' ' || c=='\r' || c=='\n' || c=='\t') continue;
        if (o + 1 >= cap) return false;
        dst[o++] = c;
    }
    if (!o) return false;
    dst[o] = 0;
    return true;
}

static bool add_key_string(char*& p, size_t& left, const char* key, const char* value) {
    char escaped[256];
    if (!value || !XmlEscape(value, escaped, sizeof(escaped))) return false;
    return append(p,left,"<key>") && append(p,left,key) && append(p,left,"</key><string>") &&
           append(p,left,escaped) && append(p,left,"</string>");
}

static bool add_key_data(char*& p, size_t& left, const char* key, const char* b64) {
    if (!b64 || !*b64) return false;
    return append(p,left,"<key>") && append(p,left,key) && append(p,left,"</key><data>") &&
           append(p,left,b64) && append(p,left,"</data>");
}

size_t BuildPairingRequestXml(const char* request, const PairMaterialView& m,
                              bool extended_pairing_errors, char* dst, size_t dst_cap) {
    if (!request || !*request || !dst || dst_cap < 2) return 0;
    char* p = dst;
    size_t left = dst_cap;
    *p = 0;

    if (!append(p,left,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                       "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                       "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"
                       "<plist version=\"1.0\"><dict>"
                       "<key>Label</key><string>iPhoneTether360</string>"
                       "<key>PairRecord</key><dict>")) return 0;

    if (!add_key_data(p,left,"DeviceCertificate",m.device_certificate_b64)) return 0;
    if (!add_key_data(p,left,"HostCertificate",m.host_certificate_b64)) return 0;
    if (!add_key_string(p,left,"HostID",m.host_id)) return 0;
    if (!add_key_data(p,left,"RootCertificate",m.root_certificate_b64)) return 0;
    if (!add_key_string(p,left,"SystemBUID",m.system_buid)) return 0;

    if (!append(p,left,"</dict><key>Request</key><string>")) return 0;
    if (!append(p,left,request)) return 0;
    if (!append(p,left,"</string><key>ProtocolVersion</key><string>2</string>")) return 0;
    if (extended_pairing_errors) {
        if (!append(p,left,"<key>PairingOptions</key><dict>"
                           "<key>ExtendedPairingErrors</key><true/>"
                           "</dict>")) return 0;
    }
    if (!append(p,left,"</dict></plist>")) return 0;
    return (size_t)(p - dst);
}

size_t BuildPairRequestXml(const PairMaterialView& m, char* dst, size_t dst_cap) {
    return BuildPairingRequestXml("Pair", m, true, dst, dst_cap);
}

size_t BuildValidatePairRequestXml(const PairMaterialView& m, char* dst, size_t dst_cap) {
    return BuildPairingRequestXml("ValidatePair", m, false, dst, dst_cap);
}

static bool has(const char* s, const char* needle) {
    return s && needle && strstr(s, needle) != 0;
}

static bool reply_matches_request(const char* xml, const char* expected) {
    if (!expected || !*expected) return true;
    char value[64];
    // Match lockdown_check_result(): Pair/ValidatePair replies must identify
    // the request they answer before absence of Error can count as success.
    if (!XmlExtractStringValue(xml, "Request", value, sizeof(value))) return false;
    return !strcmp(value, expected);
}

PairReply ParsePairingReplyXml(const char* xml, const char* expected_request) {
    if (!xml || !has(xml, "<plist")) return PAIR_REPLY_INVALID;
    if (!reply_matches_request(xml, expected_request)) return PAIR_REPLY_INVALID;

    if (has(xml, "<key>Error</key>")) {
        if (has(xml, "PairingDialogResponsePending") ||
            (!has(xml, "UserDeniedPairing") && has(xml, "PairingDialog")))
            return PAIR_REPLY_PENDING_TRUST;
        if (has(xml, "UserDeniedPairing")) return PAIR_REPLY_DENIED;
        if (has(xml, "InvalidHostID") || has(xml, "MissingHostID") || has(xml, "InvalidPairRecord"))
            return PAIR_REPLY_INVALID_HOST;
        return PAIR_REPLY_ERROR;
    }
    // iOS 5+ may omit Result entirely; absence of Error is success when Request matches.
    return PAIR_REPLY_SUCCESS;
}

PairReply ParsePairReplyXml(const char* xml) {
    return ParsePairingReplyXml(xml, "Pair");
}

PairReply ParseValidatePairReplyXml(const char* xml) {
    return ParsePairingReplyXml(xml, "ValidatePair");
}

} // namespace it360
