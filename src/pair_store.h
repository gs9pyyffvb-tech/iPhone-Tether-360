#pragma once
#include <stddef.h>
#include "pair_identity.h"

namespace it360 {

static const size_t kEscrowBagB64Cap = 16384;
static const size_t kPairRecordTextCap = 65536;

struct StoredPairRecord {
    PairIdentity identity;
    char udid[96];
    char wifi_address[64];
    char escrow_bag_b64[kEscrowBagB64Cap];
};

void ClearStoredPairRecord(StoredPairRecord& record);
size_t SerializePairRecord(const StoredPairRecord& record, char* dst, size_t cap);
bool DeserializePairRecord(const char* text, StoredPairRecord& record);

// The Xbox implementation tries HDD first, then USB roots. The host build uses
// /tmp solely so persistence behavior can be regression-tested without console hardware.
bool SavePairRecord(const StoredPairRecord& record);
bool LoadPairRecord(const char* udid, StoredPairRecord& record);
bool DeletePairRecord(const char* udid);

} // namespace it360
