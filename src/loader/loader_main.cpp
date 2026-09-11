#include "platform/xbox_platform.h"
#include "shared/app_path.h"
#include "shared/boot_log.h"
#include "shared/notify.h"
#include "shared/version.h"

namespace {

static const DWORD kResidentSystemDllFlags = 0x0000000Au;

static bool CoreAlreadyResident(const char* absolute_core_path) {
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    HMODULE module = 0;
    NTSTATUS status = XexGetModuleHandle("Core.xex", &module);
    it360_boot_log::Hex32("LOADER03A | XexGetModuleHandle(Core.xex) NTSTATUS = ", static_cast<uint32_t>(status));
    if (!it360_platform::FailedStatus(status) && module) return true;
    if (!it360_platform::FailedStatus(status) && !module)
        it360_boot_log::Line("LOADER03A | XexGetModuleHandle(Core.xex) returned success with NULL module");

    module = 0;
    status = XexGetModuleHandle("Core.exe", &module);
    it360_boot_log::Hex32("LOADER03B | XexGetModuleHandle(Core.exe) NTSTATUS = ", static_cast<uint32_t>(status));
    if (!it360_platform::FailedStatus(status) && module) return true;
    if (!it360_platform::FailedStatus(status) && !module)
        it360_boot_log::Line("LOADER03B | XexGetModuleHandle(Core.exe) returned success with NULL module");

    if (absolute_core_path && absolute_core_path[0]) {
        module = 0;
        status = XexGetModuleHandle(absolute_core_path, &module);
        it360_boot_log::Hex32("LOADER03C | XexGetModuleHandle(absolute Core.xex) NTSTATUS = ", static_cast<uint32_t>(status));
        if (!it360_platform::FailedStatus(status) && module) return true;
        if (!it360_platform::FailedStatus(status) && !module)
            it360_boot_log::Line("LOADER03C | XexGetModuleHandle(absolute Core.xex) returned success with NULL module");
    }

    return false;
#else
    (void)absolute_core_path;
    return false;
#endif
}

static int Fail(const char* log_line, const char* notification) {
    it360_boot_log::Line(log_line);
    it360_notify::Show(notification);
    it360_boot_log::Line("LOADER | Returning to Aurora after failure");
    it360_boot_log::Close();
    return 1;
}

} // namespace

int main() {
    char app_directory[512];
    char boot_log_path[560];
    char core_path[560];

    it360_notify::Show("Starting iPhoneTether360...");

    it360_platform::ResolveStatus app_path_status;
    if (!it360_app_path::ResolveAppDirectory(app_directory, sizeof(app_directory), &app_path_status)) {
        if (app_path_status.operation != it360_platform::ResolveOperationNone) {
            if (app_path_status.has_status)
                DbgPrint("[iPhoneTether360:LOADER] app path failed operation=%s NTSTATUS=0x%08x\n",
                         it360_platform::ResolveOperationName(app_path_status.operation),
                         static_cast<unsigned>(app_path_status.status));
            else
                DbgPrint("[iPhoneTether360:LOADER] app path failed operation=%s status=unavailable\n",
                         it360_platform::ResolveOperationName(app_path_status.operation));
        } else {
            DbgPrint("[iPhoneTether360:LOADER] app path failed after image-path resolution; no OS status returned\n");
        }
        it360_notify::Show("iPhoneTether360: App path not found");
        return 1;
    }

    if (!it360_app_path::Join(app_directory, "Boot.log", boot_log_path, sizeof(boot_log_path))) {
        it360_notify::Show("iPhoneTether360: Boot.log path failed");
        return 1;
    }

    uint32_t boot_open_status = 0;
    if (!it360_boot_log::Open(boot_log_path, &boot_open_status)) {
        if (boot_open_status)
            DbgPrint("[iPhoneTether360:LOADER] NtCreateFile(Boot.log) failed status=0x%08x\n",
                     static_cast<unsigned>(boot_open_status));
        else
            DbgPrint("[iPhoneTether360:LOADER] NtCreateFile(Boot.log) did not produce a usable handle | status=unavailable\n");
        it360_notify::Show("iPhoneTether360: Boot.log could not be opened");
        return 1;
    }

    it360_boot_log::Line("");
    it360_boot_log::Line("============================================================");
    it360_boot_log::Line(IT360_PRODUCT_VERSION " Loader session");
    it360_boot_log::Line("============================================================");
    it360_boot_log::Line("LOADER00 | Loader entered");
    it360_boot_log::Line("LOADER01 | App directory resolved");
    it360_boot_log::Line("LOADER02 | Startup indication queued");

    if (!it360_app_path::Join(app_directory, "Core.xex", core_path, sizeof(core_path))) {
        return Fail("LOADER03 | Core path construction failed",
                    "iPhoneTether360: Core path failed");
    }

    it360_boot_log::Line("LOADER03 | Checking for resident Core");
    if (CoreAlreadyResident(core_path)) {
        it360_boot_log::Line("LOADER04 | Core already resident; duplicate load blocked");
        it360_notify::Show("iPhoneTether360 Already Running");
        it360_boot_log::Line("LOADER05 | Returning to Aurora");
        it360_boot_log::Close();
        return 0;
    }

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    HMODULE core_module = 0;
    it360_boot_log::Line("LOADER04 | Core not resident");
    it360_boot_log::Line("LOADER05 | Calling XexLoadImage for Core.xex");

    const NTSTATUS status = XexLoadImage(
        core_path,
        kResidentSystemDllFlags,
        0u,
        &core_module
    );

    it360_boot_log::Hex32("LOADER06 | XexLoadImage status = ", static_cast<uint32_t>(status));

    if (it360_platform::FailedStatus(status) || !core_module) {
        if (!it360_platform::FailedStatus(status) && !core_module)
            it360_boot_log::Line("LOADER07 | XexLoadImage returned success with NULL module");
        return Fail("LOADER07 | Core load failed after XexLoadImage",
                    "iPhoneTether360: Core failed to load");
    }

    it360_boot_log::Line("LOADER07 | Core load succeeded");
    it360_boot_log::Line("LOADER08 | Returning to Aurora");
    it360_boot_log::Close();
    return 0;
#else
    it360_boot_log::Line("LOADER04 | Host syntax path complete");
    it360_boot_log::Close();
    return 0;
#endif
}
