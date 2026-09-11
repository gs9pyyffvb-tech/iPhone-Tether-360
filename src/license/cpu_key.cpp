#include "license/cpu_key.h"
namespace it360_license {
namespace {
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
typedef ULONGLONG (*ExExpansionCallFn)(DWORD, ULONGLONG, ULONGLONG, ULONGLONG, ULONGLONG);
static const DWORD kHvppExpansionId = 0x48565050u; // HVPP installed by XeUnshackle
static const ULONGLONG kFuseBase = 0x8000020000020000ULL;
static const ULONGLONG kPeekQword = 3ULL;
static ULONGLONG FuseAddress(unsigned line) {
    return kFuseBase + (static_cast<ULONGLONG>(line) * 0x200ULL);
}
static void StoreBe64(BYTE* out, ULONGLONG value) {
    for (unsigned i = 0; i < 8; ++i)
        out[i] = static_cast<BYTE>(value >> (56u - i * 8u));
}
#endif
}
const char* CpuKeyFailureName(CpuKeyFailure failure) {
    switch (failure) {
    case CpuKeyFailureNone: return "none";
    case CpuKeyFailureInvalidArgument: return "invalid argument";
    case CpuKeyFailureResolveExpansionCall: return "resolve ExExpansionCall";
    case CpuKeyFailureInvalidFuseRead: return "HVPP fuse read";
    default: return "unknown";
    }
}

void SecureZero(void* data, size_t bytes) {
    volatile BYTE* p = static_cast<volatile BYTE*>(data);
    while (p && bytes--) *p++ = 0;
}
bool ReadCpuKey(BYTE out_key[16], CpuKeyDiagnostic* diagnostic) {
    if (diagnostic) *diagnostic = CpuKeyDiagnostic();
    if (!out_key) {
        if (diagnostic) diagnostic->failure = CpuKeyFailureInvalidArgument;
        return false;
    }
    SecureZero(out_key, 16u);
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    void* proc = 0;
    it360_platform::ResolveStatus resolve;
    if (!it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 780u, &proc, &resolve) || !proc) {
        if (diagnostic) {
            diagnostic->failure = CpuKeyFailureResolveExpansionCall;
            diagnostic->resolve = resolve;
        }
        return false;
    }
    ExExpansionCallFn call = reinterpret_cast<ExExpansionCallFn>(proc);
    const ULONGLONG line3 = call(kHvppExpansionId, kPeekQword, FuseAddress(3u), 0ULL, 0ULL);
    const ULONGLONG line5 = call(kHvppExpansionId, kPeekQword, FuseAddress(5u), 0ULL, 0ULL);
    if ((line3 == 0ULL && line5 == 0ULL) ||
        (line3 == ~0ULL && line5 == ~0ULL)) {
        if (diagnostic) diagnostic->failure = CpuKeyFailureInvalidFuseRead;
        return false;
    }
    StoreBe64(out_key, line3);
    StoreBe64(out_key + 8, line5);
    return true;
#else
    if (diagnostic) diagnostic->failure = CpuKeyFailureResolveExpansionCall;
    return false;
#endif
}
}
