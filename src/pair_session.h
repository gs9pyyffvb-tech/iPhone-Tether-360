#pragma once
#include <stddef.h>
#include <stdint.h>
#include "pair_identity.h"
#include "pair_protocol.h"

namespace it360 {

enum PairSessionState {
    PAIR_SESSION_IDLE = 0,
    PAIR_SESSION_GET_VALUE_SENT,
    PAIR_SESSION_PAIR_SENT,
    PAIR_SESSION_PENDING_TRUST,
    PAIR_SESSION_PAIRED,
    PAIR_SESSION_DENIED,
    PAIR_SESSION_FAILED
};

// lockdownd plist helpers. GetValue replies return the requested datum under
// the generic "Value" key, regardless of which Key was requested.
size_t BuildGetValueFrame(const char* key, uint8_t* out, size_t cap);
size_t BuildGetDevicePublicKeyFrame(uint8_t* out, size_t cap);
size_t BuildGetUniqueDeviceIdFrame(uint8_t* out, size_t cap);
size_t BuildGetWiFiAddressFrame(uint8_t* out, size_t cap);
bool ParseGetValueStringXml(const char* xml, char* dst, size_t cap);
size_t ParseDevicePublicKeyXml(const char* xml, uint8_t* dst, size_t cap);

size_t BuildPairFrame(const PairIdentity& identity, uint8_t* out, size_t cap);
size_t BuildValidatePairFrame(const PairIdentity& identity, uint8_t* out, size_t cap);

class PairSession {
public:
    PairSession();
    void Reset();
    PairSessionState State() const { return state_; }

    size_t Begin(uint8_t* frame, size_t cap);
    size_t OnDevicePublicKeyReply(const char* xml, const PairCryptoBackend& crypto,
                                  uint8_t* pair_frame, size_t pair_frame_cap);
    PairReply OnPairReply(const char* xml);
    const PairIdentity& Identity() const { return identity_; }

private:
    PairSessionState state_;
    PairIdentity identity_;
    uint8_t device_public_key_[4096];
};

} // namespace it360
