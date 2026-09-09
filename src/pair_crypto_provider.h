#pragma once
#include "pair_identity.h"
namespace it360 {
// Loads the Xbox 360 17559 pairing backend. Root/host keys are embedded once per
// build and the iPhone-specific DeviceCertificate is constructed and signed on
// the console at runtime using XeCrypt RSA/SHA primitives.
bool LoadPairCryptoBackend(PairCryptoBackend* out);
}
