#pragma once
#include <stddef.h>
#include <stdint.h>

namespace it360 {

// Values are base64(PEM bytes), ready to place inside XML-plist <data>.
struct PairIdentity {
    char host_id[64];
    char system_buid[64];
    char root_certificate_b64[8192];
    char root_private_key_b64[8192];
    char host_certificate_b64[8192];
    char host_private_key_b64[8192];
    char device_certificate_b64[8192];
};

bool FormatUuidV4(const uint8_t random16[16], char out[64]);
size_t Base64Encode(const uint8_t* src, size_t len, char* dst, size_t cap);

// Crypto implementation is deliberately backend-neutral so Xbox target code does
// not depend on desktop OpenSSL. The backend owns opaque key/cert objects.
// device_public_key_pem is the PEM public key returned by lockdownd GetValue
// (DevicePublicKey). The generated DeviceCertificate must embed that key and be
// signed by the generated root key.
struct PairCryptoBackend {
    bool (*random_bytes)(uint8_t* out, size_t len);
    bool (*generate_rsa_2048_key)(void** key_out);
    bool (*make_self_signed_root)(void* root_key, void** cert_out);
    bool (*make_host_cert)(void* host_key, void* root_key, void* root_cert, void** cert_out);
    bool (*make_device_cert)(const uint8_t* device_public_key_pem, size_t device_public_key_len,
                             void* root_key, void* root_cert, void** cert_out);
    bool (*export_private_key_pem)(void* key, uint8_t* out, size_t cap, size_t* written);
    bool (*export_certificate_pem)(void* cert, uint8_t* out, size_t cap, size_t* written);
    void (*free_key)(void* key);
    void (*free_cert)(void* cert);
};

bool GeneratePairIdentity(const PairCryptoBackend& crypto,
                          const uint8_t* device_public_key_pem,
                          size_t device_public_key_len,
                          PairIdentity& out);

} // namespace it360
