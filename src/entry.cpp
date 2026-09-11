#include "early_trace.h"
#include "shared/version.h"

#include <stdint.h>

extern "C" int DllMain(
    unsigned int module_handle,
    unsigned int reason,
    unsigned int reserved
);

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)

extern "C" void crtinit();

typedef void (*It360CtorFn)(void);
extern "C" It360CtorFn __CTOR_LIST__[];

namespace {

static int gCrtInitialized = 0;

static void RunGlobalConstructors() {
    unsigned count =
        static_cast<unsigned>(
            reinterpret_cast<uintptr_t>(
                __CTOR_LIST__[0]
            )
        );

    if (count == 0xFFFFFFFFu) {
        count = 0;

        while (__CTOR_LIST__[count + 1] != 0)
            ++count;
    }

    it360_early_trace::WriteHex32(
        "ENTRY 03A: global constructor count=",
        count
    );

    for (unsigned i = count; i >= 1; --i) {
        it360_early_trace::WriteHex32(
            "ENTRY CTOR BEFORE index=",
            i
        );

        it360_early_trace::WriteHex32(
            "ENTRY CTOR BEFORE address=",
            static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(
                    __CTOR_LIST__[i]
                )
            )
        );

        __CTOR_LIST__[i]();

        it360_early_trace::WriteHex32(
            "ENTRY CTOR AFTER index=",
            i
        );
    }
}

} // namespace

extern "C" void _start(
    unsigned int Handle,
    unsigned int Reason,
    unsigned int Reserved
) {
    it360_early_trace::BeginSessionLiteral(
        IT360_VERSION_STRING " Core ENTRY 00: _start reached before CRT"
    );

    it360_early_trace::WriteHex32(
        "ENTRY 00A: module handle=",
        Handle
    );

    it360_early_trace::WriteHex32(
        "ENTRY 00B: entry reason=",
        Reason
    );

    if (!gCrtInitialized) {
        it360_early_trace::WriteLiteral(
            "ENTRY 01: about to call OpenXeChain crtinit"
        );

        crtinit();

        it360_early_trace::WriteLiteral(
            "ENTRY 02: crtinit returned"
        );

        it360_early_trace::WriteLiteral(
            "ENTRY 03: about to run global constructors"
        );

        RunGlobalConstructors();

        it360_early_trace::WriteLiteral(
            "ENTRY 04: global constructors completed"
        );

        gCrtInitialized = 1;

        if (it360_early_trace::ConfigureApplicationPaths()) {
            it360_early_trace::BeginSessionLiteral(
                IT360_VERSION_STRING " Core diagnostic session started"
            );
        }
    } else {
        it360_early_trace::WriteLiteral(
            "ENTRY 04A: CRT/global constructors already initialized"
        );
    }

    it360_early_trace::WriteLiteral(
        "ENTRY 05: about to call DllMain"
    );

    const int result =
        DllMain(Handle, Reason, Reserved);

    it360_early_trace::WriteHex32(
        "ENTRY 06: DllMain returned result=",
        static_cast<uint32_t>(result)
    );
}

#else

extern "C" void _start(
    unsigned int Handle,
    unsigned int Reason,
    unsigned int Reserved
) {
    (void)DllMain(Handle, Reason, Reserved);
}

#endif
