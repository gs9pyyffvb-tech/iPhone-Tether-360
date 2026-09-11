#include "license/license_id.h"
#include "license/cpu_key.h"
#include "sha256.h"
#include <string.h>
namespace it360_license {
bool DeriveLicenceId(char out_hex[65], CpuKeyDiagnostic* diagnostic) {
    if (diagnostic) *diagnostic = CpuKeyDiagnostic();
    if (!out_hex) {
        if (diagnostic) diagnostic->failure = CpuKeyFailureInvalidArgument;
        return false;
    }
    out_hex[0] = 0;
    static const char domain[] = "iPhoneTether360.ConsoleLicence.v1";
    BYTE key[16];
    BYTE digest[32];
    BYTE input[(sizeof(domain) - 1u) + 16u];
    if (!ReadCpuKey(key, diagnostic)) return false;
    memcpy(input, domain, sizeof(domain) - 1u);
    memcpy(input + sizeof(domain) - 1u, key, sizeof(key));
    sha256::Hash(input, sizeof(input), digest);
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32u; ++i) {
        out_hex[i * 2u] = hex[(digest[i] >> 4) & 0x0Fu];
        out_hex[i * 2u + 1u] = hex[digest[i] & 0x0Fu];
    }
    out_hex[64] = 0;
    SecureZero(key, sizeof(key));
    SecureZero(digest, sizeof(digest));
    SecureZero(input, sizeof(input));
    return true;
}
}
