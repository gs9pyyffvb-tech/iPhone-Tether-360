#include "pair_crypto_provider.h"
#include "platform/xbox_platform.h"
#include "pair_identity_generated.h"
#include "sha256.h"
#include "der_x509.h"
#include <string.h>


namespace it360 {
namespace {

enum KeyKind { KEY_ROOT = 1, KEY_HOST = 2 };
enum CertKind { CERT_ROOT = 1, CERT_HOST = 2, CERT_DEVICE = 3 };
struct KeyObject { KeyKind kind; };
struct CertObject { CertKind kind; };

static KeyObject gRootKey = { KEY_ROOT };
static KeyObject gHostKey = { KEY_HOST };
static CertObject gRootCert = { CERT_ROOT };
static CertObject gHostCert = { CERT_HOST };
static CertObject gDeviceCert = { CERT_DEVICE };
static char gDeviceCertPem[4096];
static size_t gDeviceCertPemLen = 0;
static unsigned gKeyIssue = 0;
static unsigned gRandomIssue = 0;

#ifdef IT360_XBOX
typedef bool (*RsaPrvFn)(const uint64_t*, uint64_t*, const void*);
typedef void (*Sha1FnRaw)(const unsigned char*, int, const unsigned char*, int,
                          const unsigned char*, int, unsigned char*, int);
static RsaPrvFn gRsa = 0;
static Sha1FnRaw gSha1 = 0;

static bool ResolveOrdinal(DWORD ordinal, void** out) {
    return it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", ordinal, out);
}

static bool Sha1Digest(const uint8_t* data, size_t len, uint8_t out[20]) {
    if (!gSha1 || !data || !out || len > 0x7FFFFFFFu) return false;
    gSha1(data, (int)len, 0, 0, 0, 0, out, 20);
    return true;
}

static void ReverseQwords(const uint8_t* in, uint8_t* out, size_t n) {
    for (size_t q = 0; q < n / 8; ++q)
        memcpy(out + q * 8, in + (n / 8 - 1 - q) * 8, 8);
}

static bool SignRootSha256(const uint8_t* tbs, size_t n, uint8_t sig[256]) {
    if (!gRsa || !tbs || !sig) return false;
    uint8_t hash[32];
    sha256::Hash(tbs, n, hash);
    static const uint8_t digestInfo[] = {
        0x30,0x31,0x30,0x0D,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,
        0x05,0x00,0x04,0x20
    };
    uint8_t em[256];
    memset(em, 0xFF, sizeof(em));
    em[0] = 0;
    em[1] = 1;
    const size_t tail = sizeof(digestInfo) + sizeof(hash);
    const size_t sep = sizeof(em) - tail - 1;
    em[sep] = 0;
    memcpy(em + sep + 1, digestInfo, sizeof(digestInfo));
    memcpy(em + sep + 1 + sizeof(digestInfo), hash, sizeof(hash));

    IT360_ALIGN8 uint8_t inQ[256];
    IT360_ALIGN8 uint8_t outQ[256];
    ReverseQwords(em, inQ, sizeof(inQ));
    memset(outQ, 0, sizeof(outQ));
    if (!gRsa((const uint64_t*)inQ, (uint64_t*)outQ,
              pair_identity_generated::kRootPrivateXeCrypt)) return false;
    ReverseQwords(outQ, sig, sizeof(outQ));
    return true;
}
#endif

static bool FixedRandom(uint8_t* out, size_t len) {
    if (!out || !len) return false;
    const uint8_t* seed = (gRandomIssue++ & 1)
        ? pair_identity_generated::kSystemBuidUuidBytes
        : pair_identity_generated::kHostUuidBytes;
    for (size_t i = 0; i < len; ++i) out[i] = seed[i % 16];
    return true;
}

static bool GenerateKey(void** key_out) {
    if (!key_out) return false;
    if (gKeyIssue == 0) *key_out = &gRootKey;
    else if (gKeyIssue == 1) *key_out = &gHostKey;
    else return false;
    ++gKeyIssue;
    return true;
}

static bool MakeRoot(void* root_key, void** cert_out) {
    if (!cert_out || root_key != &gRootKey) return false;
    *cert_out = &gRootCert;
    return true;
}

static bool MakeHost(void* host_key, void* root_key, void* root_cert, void** cert_out) {
    if (!cert_out || host_key != &gHostKey || root_key != &gRootKey || root_cert != &gRootCert)
        return false;
    *cert_out = &gHostCert;
    return true;
}

static bool MakeDevice(const uint8_t* device_public_key_pem, size_t device_public_key_len,
                       void* root_key, void* root_cert, void** cert_out) {
    if (!cert_out || !device_public_key_pem || !device_public_key_len ||
        root_key != &gRootKey || root_cert != &gRootCert) return false;
#ifdef IT360_XBOX
    gDeviceCertPemLen = 0;
    if (!der_x509::BuildDeviceCertificatePem(device_public_key_pem, device_public_key_len,
                                              Sha1Digest, SignRootSha256,
                                              gDeviceCertPem, sizeof(gDeviceCertPem),
                                              &gDeviceCertPemLen)) return false;
    *cert_out = &gDeviceCert;
    return true;
#else
    (void)device_public_key_pem;
    (void)device_public_key_len;
    return false;
#endif
}

static bool CopyPem(const char* pem, uint8_t* out, size_t cap, size_t* written) {
    if (!pem || !out || !written) return false;
    const size_t n = strlen(pem);
    if (!n || n > cap) return false;
    memcpy(out, pem, n);
    *written = n;
    return true;
}

static bool ExportPrivate(void* key, uint8_t* out, size_t cap, size_t* written) {
    if (key == &gRootKey)
        return CopyPem(pair_identity_generated::kRootPrivateKeyPem, out, cap, written);
    if (key == &gHostKey)
        return CopyPem(pair_identity_generated::kHostPrivateKeyPem, out, cap, written);
    return false;
}

static bool ExportCert(void* cert, uint8_t* out, size_t cap, size_t* written) {
    if (cert == &gRootCert)
        return CopyPem(pair_identity_generated::kRootCertificatePem, out, cap, written);
    if (cert == &gHostCert)
        return CopyPem(pair_identity_generated::kHostCertificatePem, out, cap, written);
    if (cert == &gDeviceCert && gDeviceCertPemLen) {
        if (gDeviceCertPemLen > cap) return false;
        memcpy(out, gDeviceCertPem, gDeviceCertPemLen);
        *written = gDeviceCertPemLen;
        return true;
    }
    return false;
}

static void FreeKey(void*) {}
static void FreeCert(void*) {}

} // namespace

bool LoadPairCryptoBackend(PairCryptoBackend* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
#ifdef IT360_XBOX
    gRsa = 0;
    gSha1 = 0;
    // Retained/validated 17559 exports from the earlier Batch 9B diagnostic.
    if (!ResolveOrdinal(364, (void**)&gRsa) || !ResolveOrdinal(402, (void**)&gSha1))
        return false;
#else
    return false;
#endif
    gKeyIssue = 0;
    gRandomIssue = 0;
    gDeviceCertPemLen = 0;
    out->random_bytes = FixedRandom;
    out->generate_rsa_2048_key = GenerateKey;
    out->make_self_signed_root = MakeRoot;
    out->make_host_cert = MakeHost;
    out->make_device_cert = MakeDevice;
    out->export_private_key_pem = ExportPrivate;
    out->export_certificate_pem = ExportCert;
    out->free_key = FreeKey;
    out->free_cert = FreeCert;
    return true;
}

} // namespace it360
