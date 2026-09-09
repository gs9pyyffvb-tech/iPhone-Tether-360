#include "platform/xbox_platform.h"
#include "usbmux_probe.h"
#include "diag.h"

namespace {

static uint32_t DelayedInitialize(void*) {
    it360_diag::BootCheckpoint(
        "BOOT 08: delayed initialization worker entered"
    );

    it360_diag::BootCheckpoint(
        "BOOT 09: waiting 3000ms before kernel USB initialization"
    );

    it360_platform::SleepMs(3000);

    it360_diag::BootCheckpoint(
        "BOOT 10: initialization delay completed"
    );

    it360_diag::BootCheckpoint(
        "BOOT 11: about to call InstallLockdownProbe"
    );

    const bool installed =
        iphone_probe::InstallLockdownProbe();

    it360_diag::BootCheckpoint(
        "BOOT 12: InstallLockdownProbe returned result=%u",
        installed ? 1u : 0u
    );

    if (!installed) {
        it360_diag::Log(
            "[iPhoneTether360:B9C-OXC] installation FAILED\n"
        );

        it360_diag::Notify(
            "iPhoneTether360: plugin installation failed"
        );

        it360_diag::BootCheckpoint(
            "BOOT 13: initialization FAILED"
        );
    } else {
        it360_diag::BootCheckpoint(
            "BOOT 13: initialization SUCCESS"
        );

        it360_diag::Notify(
            "iPhoneTether360 ready - connect iPhone"
        );
    }

    return 0;
}

} // namespace

extern "C" int DllMain(unsigned int module_handle, unsigned int reason, unsigned int) {
    static const unsigned kProcessAttach = 1;

    if (reason != kProcessAttach)
        return 1;

    (void)module_handle;

    it360_diag::BootCheckpoint(
        "BOOT 01: DllMain entered"
    );

    it360_diag::BootCheckpoint(
        "BOOT 02: starting asynchronous diagnostics"
    );

    it360_diag::Init();

    it360_diag::BootCheckpoint(
        "BOOT 03: diagnostics Init returned"
    );

    it360_diag::BootCheckpoint(
        "BOOT 04: reading Xbox kernel version"
    );

    const WORD build =
        it360_platform::KernelBuild();

    it360_diag::BootCheckpoint(
        "BOOT 05: Xbox kernel build=%u",
        static_cast<unsigned>(build)
    );

    it360_diag::Log(
        "[iPhoneTether360:B9C-OXC] Pair/Trust + standalone iPhone USB tether stack starting; kernel=%u\n",
        static_cast<unsigned>(build)
    );

    if (build != 17559) {
        it360_diag::BootCheckpoint(
            "BOOT 06: unsupported kernel - initialization stopped"
        );

        it360_diag::Log(
            "[iPhoneTether360:B9C-OXC] unsupported kernel; no patch applied\n"
        );

        it360_diag::Notify(
            "iPhoneTether360: unsupported Xbox kernel"
        );

        return 1;
    }

    it360_diag::BootCheckpoint(
        "BOOT 06: kernel 17559 accepted"
    );

    it360_diag::BootCheckpoint(
        "BOOT 07: creating delayed initialization worker"
    );

    if (!it360_platform::StartDetachedThread(
            DelayedInitialize,
            0)) {

        it360_diag::BootCheckpoint(
            "BOOT 07 FAILED: initialization worker could not start"
        );

        it360_diag::Log(
            "[iPhoneTether360:B9C-OXC] initialization worker FAILED\n"
        );

        return 1;
    }

    it360_diag::BootCheckpoint(
        "BOOT 07 COMPLETE: returning from DllMain"
    );

    return 1;
}
