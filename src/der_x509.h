#pragma once
#include <stddef.h>
#include <stdint.h>
namespace der_x509 {
typedef bool (*SignSha256Fn)(const uint8_t* tbs,size_t tbsLen,uint8_t signature[256]);
typedef bool (*Sha1Fn)(const uint8_t* data,size_t len,uint8_t digest[20]);
bool BuildDeviceCertificatePem(const uint8_t* devicePublicPem,size_t pemLen,
                               Sha1Fn sha1,SignSha256Fn sign,
                               char* outPem,size_t outCap,size_t* outLen);
}
