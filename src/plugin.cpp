#include "platform/xbox_platform.h"
#include "early_trace.h"
#include "usbmux_probe.h"
#include "diag.h"

namespace {

static void LogThreadStartFailure(const char* context, const it360_platform::ThreadStartStatus& detail) {
    if (detail.has_status) {
        it360_diag::Log("[iPhoneTether360:CORE] %s failed operation=%s NTSTATUS=0x%08x\n",
                        context, it360_platform::ThreadStartOperationName(detail.operation),
                        static_cast<unsigned>(detail.status));
    } else {
        it360_diag::Log("[iPhoneTether360:CORE] %s failed operation=%s status=unavailable\n",
                        context, it360_platform::ThreadStartOperationName(detail.operation));
    }
}

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
        "BOOT 01A: boot trace persistence "
        "ready=%u failures=%u last_status=%08x",
        it360_early_trace::PersistentReady()
            ? 1u
            : 0u,
        static_cast<unsigned>(
            it360_early_trace::FailureCount()
        ),
        static_cast<unsigned>(
            it360_early_trace::LastStatus()
        )
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

    it360_platform::ResolveStatus kernel_status;
    const WORD build =
        it360_platform::KernelBuild(&kernel_status);
    if (!build && kernel_status.operation != it360_platform::ResolveOperationNone) {
        if (kernel_status.has_status)
            it360_diag::Log("[iPhoneTether360:CORE] kernel version lookup failed operation=%s NTSTATUS=0x%08x\n",
                            it360_platform::ResolveOperationName(kernel_status.operation),
                            static_cast<unsigned>(kernel_status.status));
        else
            it360_diag::Log("[iPhoneTether360:CORE] kernel version lookup failed operation=%s status=unavailable\n",
                            it360_platform::ResolveOperationName(kernel_status.operation));
    }

    it360_diag::BootCheckpoint(
        "BOOT 05: Xbox kernel build=%u",
        static_cast<unsigned>(build)
    );

    it360_diag::Log(
        "[iPhoneTether360:B9C-OXC] Pair/Trust + standalone "
        "iPhone USB tether stack starting; kernel=%u\n",
        static_cast<unsigned>(build)
    );

    if (build != 17559) {
        it360_diag::BootCheckpoint(
            "BOOT 06: unsupported kernel - initialization stopped"
        );

        it360_diag::Log(
            "[iPhoneTether360:B9C-OXC] unsupported kernel; "
            "no patch applied\n"
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

    it360_platform::ThreadStartStatus worker_status;
    if (!it360_platform::StartDetachedThread(
            DelayedInitialize,
            0,
            &worker_status)) {

        LogThreadStartFailure("delayed initialization worker", worker_status);

        it360_diag::BootCheckpoint(
            "BOOT 07 FAILED: initialization worker could not start"
        );

        it360_diag::Log(
            "[iPhoneTether360:B9C-OXC] initialization worker FAILED\n"
        );

        return 1;
    }

    if (worker_status.close_status_valid && it360_platform::FailedStatus(worker_status.close_status))
        it360_diag::Log("[iPhoneTether360:CORE] delayed initialization worker NtClose(handle) failed NTSTATUS=0x%08x\n",
                        static_cast<unsigned>(worker_status.close_status));

    it360_diag::BootCheckpoint(
        "BOOT 07 COMPLETE: returning from DllMain"
    );

    return 1;
}
