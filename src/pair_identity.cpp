#include "pair_identity.h"
#include <string.h>

namespace it360 {

bool FormatUuidV4(const uint8_t input[16], char out[64]) {
    if (!input || !out) return false;
    uint8_t b[16];
    memcpy(b, input, 16);
    b[6] = (uint8_t)((b[6] & 0x0F) | 0x40);
    b[8] = (uint8_t)((b[8] & 0x3F) | 0x80);
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[o++] = '-';
        out[o++] = hex[(b[i] >> 4) & 0x0F];
        out[o++] = hex[b[i] & 0x0F];
    }
    out[o] = 0;
    return o == 36;
}

size_t Base64Encode(const uint8_t* src, size_t len, char* dst, size_t cap) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!dst || (!src && len)) return 0;
    const size_t need = ((len + 2) / 3) * 4;
    if (cap <= need) return 0;
    size_t i = 0, o = 0;
    while (i < len) {
        const size_t remain = len - i;
        uint32_t a = src[i++];
        uint32_t b = (remain > 1) ? src[i++] : 0;
        uint32_t c = (remain > 2) ? src[i++] : 0;
        uint32_t v = (a << 16) | (b << 8) | c;
        dst[o++] = t[(v >> 18) & 63];
        dst[o++] = t[(v >> 12) & 63];
        dst[o++] = (remain > 1) ? t[(v >> 6) & 63] : '=';
        dst[o++] = (remain > 2) ? t[v & 63] : '=';
    }
    dst[o] = 0;
    return o;
}

static bool export_b64(bool (*fn)(void*, uint8_t*, size_t, size_t*),
                       void* obj, char* dst, size_t dstcap) {
    static uint8_t pem[6144];
    size_t n = 0;
    if (!fn || !fn(obj, pem, sizeof(pem), &n) || n == 0 || n > sizeof(pem)) return false;
    return Base64Encode(pem, n, dst, dstcap) != 0;
}

bool GeneratePairIdentity(const PairCryptoBackend& c,
                          const uint8_t* device_public_key_pem,
                          size_t device_public_key_len,
                          PairIdentity& out) {
    memset(&out, 0, sizeof(out));
    if (!device_public_key_pem || device_public_key_len == 0 ||
        !c.random_bytes || !c.generate_rsa_2048_key ||
        !c.make_self_signed_root || !c.make_host_cert || !c.make_device_cert ||
        !c.export_private_key_pem || !c.export_certificate_pem ||
        !c.free_key || !c.free_cert) return false;

    uint8_t r[16];
    if (!c.random_bytes(r, sizeof(r)) || !FormatUuidV4(r, out.host_id)) return false;
    if (!c.random_bytes(r, sizeof(r)) || !FormatUuidV4(r, out.system_buid)) return false;

    void *root_key = 0, *host_key = 0;
    void *root_cert = 0, *host_cert = 0, *device_cert = 0;
    bool ok = false;

    if (!c.generate_rsa_2048_key(&root_key)) goto done;
    if (!c.generate_rsa_2048_key(&host_key)) goto done;
    if (!c.make_self_signed_root(root_key, &root_cert)) goto done;
    if (!c.make_host_cert(host_key, root_key, root_cert, &host_cert)) goto done;
    if (!c.make_device_cert(device_public_key_pem, device_public_key_len,
                            root_key, root_cert, &device_cert)) goto done;

    if (!export_b64(c.export_private_key_pem, root_key,
                    out.root_private_key_b64, sizeof(out.root_private_key_b64))) goto done;
    if (!export_b64(c.export_certificate_pem, root_cert,
                    out.root_certificate_b64, sizeof(out.root_certificate_b64))) goto done;
    if (!export_b64(c.export_private_key_pem, host_key,
                    out.host_private_key_b64, sizeof(out.host_private_key_b64))) goto done;
    if (!export_b64(c.export_certificate_pem, host_cert,
                    out.host_certificate_b64, sizeof(out.host_certificate_b64))) goto done;
    if (!export_b64(c.export_certificate_pem, device_cert,
                    out.device_certificate_b64, sizeof(out.device_certificate_b64))) goto done;
    ok = true;

done:
    if (device_cert) c.free_cert(device_cert);
    if (host_cert) c.free_cert(host_cert);
    if (root_cert) c.free_cert(root_cert);
    if (host_key) c.free_key(host_key);
    if (root_key) c.free_key(root_key);
    if (!ok) memset(&out, 0, sizeof(out));
    return ok;
}

} // namespace it360
