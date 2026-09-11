#include "license/license_document.h"

namespace it360_license_runtime {
namespace {

static char LowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

static bool EqualNoCase(const char* a, unsigned a_len, const char* b) {
    if (!a || !b) return false;
    unsigned b_len = 0;
    while (b[b_len]) ++b_len;
    if (a_len != b_len) return false;
    for (unsigned i = 0; i < a_len; ++i)
        if (LowerAscii(a[i]) != LowerAscii(b[i])) return false;
    return true;
}

static bool IsHex64(const char* p, unsigned n) {
    if (!p || n != 64u) return false;
    for (unsigned i = 0; i < n; ++i) {
        const char c = LowerAscii(p[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static bool EqualsLicenceId(const char* p, const char id[65]) {
    if (!p || !id) return false;
    for (unsigned i = 0; i < 64u; ++i)
        if (LowerAscii(p[i]) != LowerAscii(id[i])) return false;
    return id[64] == 0;
}

} // namespace

LicenceDocumentVerdict EvaluateLicenceDocument(const char* body, unsigned body_length,
                                                const char licence_id[65]) {
    if (!body || !body_length || !licence_id || licence_id[64] != 0) return LicenceDocumentInvalid;

    unsigned off = 0;
    if (body_length >= 3u && static_cast<unsigned char>(body[0]) == 0xEFu &&
        static_cast<unsigned char>(body[1]) == 0xBBu && static_cast<unsigned char>(body[2]) == 0xBFu)
        off = 3u;

    bool recognised = false;
    bool free_mode = false;
    bool matched = false;

    while (off < body_length) {
        unsigned end = off;
        while (end < body_length && body[end] != '\r' && body[end] != '\n') ++end;

        unsigned first = off;
        while (first < end && (body[first] == ' ' || body[first] == '\t')) ++first;
        unsigned last = end;
        while (last > first && (body[last - 1u] == ' ' || body[last - 1u] == '\t')) --last;
        const unsigned n = last - first;

        if (n != 0u) {
            if (body[first] == '#') {
                // comment
            } else if (EqualNoCase(body + first, n, "list of allowed licenses:")) {
                recognised = true;
            } else if (EqualNoCase(body + first, n, "free")) {
                recognised = true;
                free_mode = true;
            } else if (IsHex64(body + first, n)) {
                recognised = true;
                if (EqualsLicenceId(body + first, licence_id)) matched = true;
            } else {
                return LicenceDocumentInvalid;
            }
        }

        while (end < body_length && (body[end] == '\r' || body[end] == '\n')) ++end;
        off = end;
    }

    if (!recognised) return LicenceDocumentInvalid;
    if (matched) return LicenceDocumentMatched;
    if (free_mode) return LicenceDocumentFree;
    return LicenceDocumentNoMatch;
}

} // namespace it360_license_runtime
