#include "platform/xbox_platform.h"
#include <string.h>
#include "hook17559.h"

namespace iphone_probe {
namespace {

// Xenon PowerPC instruction encoders used only for unconditional far branches.
static DWORD Lis(unsigned reg, DWORD address) {
    return 0x3C000000u | ((reg & 31u) << 21) | ((address >> 16) & 0xFFFFu);
}
static DWORD OriSame(unsigned reg, DWORD address) {
    return 0x60000000u | ((reg & 31u) << 21) | ((reg & 31u) << 16) | (address & 0xFFFFu);
}
static DWORD Mtctr(unsigned reg) {
    // mtspr CTR(9), rS
    const DWORD spr = ((9u & 0x1Fu) << 5) | ((9u >> 5) & 0x1Fu);
    return 0x7C0003A6u | ((reg & 31u) << 21) | (spr << 11);
}
static DWORD Bctr(bool link) { return 0x4E800420u | (link ? 1u : 0u); }
static void EmitFarBranch(DWORD* out, DWORD target, unsigned scratchReg, bool link) {
    out[0] = Lis(scratchReg, target);
    out[1] = OriSame(scratchReg, target);
    out[2] = Mtctr(scratchReg);
    out[3] = Bctr(link);
}

IT360_EXEC_SECTION static const DWORD gMatcherTrampoline[13] = {
    0x7D8802A6u,
    0x3D608010u, 0x616BCBC8u, 0x7D6903A6u, 0x4E800421u,
    0x9421FF80u, 0x3D60801Bu,
    0xF801FFD0u, 0x3C00800Du, 0x60006188u, 0x7C0903A6u, 0xE801FFD0u, 0x4E800420u
};

} // namespace

MatcherHook17559::MatcherHook17559()
    : target_(0), trampoline_(0) {
    memset(original_, 0, sizeof(original_));
}

bool MatcherHook17559::Install(void* target, void* replacement) {
    if (!target || !replacement || target_) return false;

    DWORD* src = reinterpret_cast<DWORD*>(target);

    // Fail closed if this is not the exact 17559 matcher prologue recovered
    // from the user's verified kernel. This prevents patching an unexpected build.
    const DWORD expected[4] = {
        0x7D8802A6u, // mflr r12
        0x48036A4Du, // bl 0x8010CBC8
        0x9421FF80u, // stwu r1,-0x80(r1)
        0x3D60801Bu  // lis r11,0x801B
    };
    if (memcmp(src, expected, sizeof(expected)) != 0)
        return false;

    memcpy(original_, src, sizeof(original_));
    // The relocated 17559 trampoline is immutable and pre-encoded above. This
    // avoids writing into our executable section after SynthXEX has mapped it.

    // Replace the first 16 bytes with an absolute branch to our hook.
    DWORD patch[4];
    EmitFarBranch(patch, static_cast<DWORD>(reinterpret_cast<uintptr_t>(replacement)), 11, false);
    memcpy(src, patch, sizeof(patch));
    it360_platform::FlushInstructionCache(src, sizeof(patch));

    target_ = target;
    trampoline_ = const_cast<DWORD*>(gMatcherTrampoline);
    return true;
}

} // namespace iphone_probe
