#include "base64.h"

namespace b64 {

static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t Encode(const uint8_t* in, size_t n, char* out, size_t cap, bool wrap64) {
    if (!out || (!in && n)) return 0;
    size_t o = 0;
    size_t col = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)in[i] << 16;
        int remain = (int)(n - i);
        if (remain > 1) v |= (unsigned)in[i + 1] << 8;
        if (remain > 2) v |= in[i + 2];
        char q[4] = {
            kTable[(v >> 18) & 63], kTable[(v >> 12) & 63],
            remain > 1 ? kTable[(v >> 6) & 63] : '=',
            remain > 2 ? kTable[v & 63] : '='
        };
        for (int j = 0; j < 4; ++j) {
            if (o + 2 > cap) return 0;
            out[o++] = q[j];
            if (wrap64 && ++col == 64) {
                out[o++] = '\n';
                col = 0;
            }
        }
    }
    if (wrap64 && col) {
        if (o + 1 > cap) return 0;
        out[o++] = '\n';
    }
    if (o >= cap) return 0;
    out[o] = 0;
    return o;
}

static int Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

size_t Decode(const char* in, size_t n, uint8_t* out, size_t cap) {
    if (!in || !out) return 0;
    int q[4];
    int k = 0;
    size_t o = 0;
    for (size_t i = 0; i < n; ++i) {
        const char c = in[i];
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        if (c == '=') q[k++] = -2;
        else {
            const int x = Value(c);
            if (x < 0) return 0;
            q[k++] = x;
        }
        if (k == 4) {
            if (q[0] < 0 || q[1] < 0) return 0;
            const unsigned v = ((unsigned)q[0] << 18) | ((unsigned)q[1] << 12) |
                               ((q[2] < 0 ? 0u : (unsigned)q[2]) << 6) |
                               (q[3] < 0 ? 0u : (unsigned)q[3]);
            if (o >= cap) return 0;
            out[o++] = (uint8_t)(v >> 16);
            if (q[2] != -2) {
                if (o >= cap) return 0;
                out[o++] = (uint8_t)(v >> 8);
            }
            if (q[3] != -2) {
                if (o >= cap) return 0;
                out[o++] = (uint8_t)v;
            }
            k = 0;
        }
    }
    return k ? 0 : o;
}

} // namespace b64
