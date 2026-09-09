#include "platform/xbox_platform.h"
#include <string.h>
#include "hook17559.h"
#include "diag.h"

namespace iphone_probe {
namespace {

static DWORD Lis(unsigned reg, DWORD address) {
    return 0x3C000000u |
           ((reg & 31u) << 21) |
           ((address >> 16) & 0xFFFFu);
}

static DWORD OriSame(unsigned reg, DWORD address) {
    return 0x60000000u |
           ((reg & 31u) << 21) |
           ((reg & 31u) << 16) |
           (address & 0xFFFFu);
}

static DWORD Mtctr(unsigned reg) {
    const DWORD spr =
        ((9u & 0x1Fu) << 5) |
        ((9u >> 5) & 0x1Fu);

    return 0x7C0003A6u |
           ((reg & 31u) << 21) |
           (spr << 11);
}

static DWORD Bctr(bool link) {
    return 0x4E800420u |
           (link ? 1u : 0u);
}

static void EmitFarBranch(
    DWORD* out,
    DWORD target,
    unsigned scratchReg,
    bool link
) {
    out[0] = Lis(scratchReg, target);
    out[1] = OriSame(scratchReg, target);
    out[2] = Mtctr(scratchReg);
    out[3] = Bctr(link);
}

IT360_EXEC_SECTION static const DWORD gMatcherTrampoline[13] = {
    0x7D8802A6u,
    0x3D608010u,
    0x616BCBC8u,
    0x7D6903A6u,
    0x4E800421u,

    0x9421FF80u,
    0x3D60801Bu,

    0xF801FFD0u,
    0x3C00800Du,
    0x60006188u,
    0x7C0903A6u,
    0xE801FFD0u,
    0x4E800420u
};

} // namespace

MatcherHook17559::MatcherHook17559()
    : target_(0),
      trampoline_(const_cast<DWORD*>(gMatcherTrampoline)) {
    memset(original_, 0, sizeof(original_));
}

bool MatcherHook17559::Install(
    void* target,
    void* replacement
) {
    it360_diag::BootCheckpoint(
        "HOOK 01: MatcherHook17559::Install entered target=%08x replacement=%08x",
        static_cast<unsigned>(
            reinterpret_cast<uintptr_t>(target)
        ),
        static_cast<unsigned>(
            reinterpret_cast<uintptr_t>(replacement)
        )
    );

    if (!target || !replacement || target_) {
        it360_diag::BootCheckpoint(
            "HOOK 02: invalid hook arguments/state"
        );
        return false;
    }

    DWORD* src =
        reinterpret_cast<DWORD*>(target);

    const DWORD expected[4] = {
        0x7D8802A6u,
        0x48036A4Du,
        0x9421FF80u,
        0x3D60801Bu
    };

    it360_diag::BootCheckpoint(
        "HOOK 03: checking 17559 matcher prologue"
    );

    if (memcmp(
            src,
            expected,
            sizeof(expected)) != 0) {

        it360_diag::BootCheckpoint(
            "HOOK 04: matcher prologue MISMATCH"
        );

        return false;
    }

    it360_diag::BootCheckpoint(
        "HOOK 05: matcher prologue verified"
    );

    memcpy(
        original_,
        src,
        sizeof(original_)
    );

    it360_diag::BootCheckpoint(
        "HOOK 06: original matcher bytes saved"
    );

    DWORD patch[4];

    EmitFarBranch(
        patch,
        static_cast<DWORD>(
            reinterpret_cast<uintptr_t>(
                replacement
            )
        ),
        11,
        false
    );

    it360_diag::BootCheckpoint(
        "HOOK 07: absolute branch generated; kernel patch next"
    );

    // Do not perform filesystem logging between the actual kernel write and
    // cache synchronization.
    memcpy(
        src,
        patch,
        sizeof(patch)
    );

    it360_platform::FlushInstructionCache(src, sizeof(patch));

    target_ = target;

    it360_diag::BootCheckpoint(
        "HOOK 08: kernel patch written and instruction cache synchronized"
    );

    return true;
}

} // namespace iphone_probe
