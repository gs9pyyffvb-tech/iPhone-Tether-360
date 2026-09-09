#pragma once
#include "platform/xbox_platform.h"

namespace iphone_probe {

// Small 17559-specific hook. It is intentionally not a general detour library.
// It knows the exact first four instructions of the user's verified 17559
// dynamic USB matcher and builds a fixed trampoline for that function only.
class MatcherHook17559 {
public:
    MatcherHook17559();
    bool Install(void* target, void* replacement);
    void* Original() const { return trampoline_; }

private:
    void* target_;
    void* trampoline_;
    DWORD original_[4];
};

} // namespace iphone_probe
