#include "platform/xbox_platform.h"
#include "usbmux_probe.h"
#include "diag.h"

struct LoaderEntryPrefix {
    BYTE reserved[0x40];
    WORD loadCount;
};
#if defined(IT360_OPENXECHAIN)
IT360_STATIC_ASSERT(loader_loadcount_offset_40, offsetof(LoaderEntryPrefix, loadCount) == 0x40);
#endif

// OpenXeChain Newlib's Xbox CRT calls DllMain with three 32-bit arguments for
// DLL modules. Keep this signature exactly aligned with that CRT contract.
extern "C" int DllMain(unsigned int module_handle, unsigned int reason, unsigned int) {
    static const unsigned kProcessAttach = 1;
    if (reason != kProcessAttach) return 1;

    LoaderEntryPrefix* entry = reinterpret_cast<LoaderEntryPrefix*>(
        static_cast<uintptr_t>(module_handle));
    if (entry) entry->loadCount = 1;

    it360_diag::Init();
    const WORD build = it360_platform::KernelBuild();
    it360_diag::Log("[iPhoneTether360:B9C-OXC] Pair/Trust + standalone iPhone USB tether stack starting; kernel=%u\n",
                    static_cast<unsigned>(build));
    if (build != 17559) {
        it360_diag::Log("[iPhoneTether360:B9C-OXC] unsupported kernel; no patch applied\n");
        it360_diag::Notify("iPhoneTether360: unsupported Xbox kernel");
        return 1;
    }
    if (!iphone_probe::InstallLockdownProbe()) {
        it360_diag::Log("[iPhoneTether360:B9C-OXC] installation FAILED\n");
        it360_diag::Notify("iPhoneTether360: plugin installation failed");
    } else {
        it360_diag::Notify("iPhoneTether360 ready - connect iPhone");
    }
    return 1;
}
