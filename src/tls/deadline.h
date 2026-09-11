#pragma once
#include "platform/xbox_platform.h"
namespace it360_tls {
class Deadline {
public:
    explicit Deadline(DWORD timeout_ms)
        : start_(it360_platform::MonotonicMs()), timeout_(timeout_ms) {}
    bool Expired() const {
        const uint64_t now = it360_platform::MonotonicMs();
        return start_ && now && now >= start_ + timeout_;
    }
    DWORD RemainingMs() const {
        if (!start_) return timeout_;
        const uint64_t now = it360_platform::MonotonicMs();
        if (!now || now <= start_) return timeout_;
        const uint64_t elapsed = now - start_;
        return elapsed >= timeout_ ? 0u : static_cast<DWORD>(timeout_ - elapsed);
    }
private:
    uint64_t start_;
    DWORD timeout_;
};
}
