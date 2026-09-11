#pragma once
#include <stddef.h>
#include "bearssl.h"
namespace it360_tls {
const br_x509_trust_anchor* SectigoTrustAnchors(size_t* count);
}
