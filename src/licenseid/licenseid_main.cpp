#include "platform/xbox_platform.h"
#include "shared/app_path.h"
#include "shared/notify.h"
#include "shared/version.h"
#include "license/license_id.h"
#include "license/cpu_key.h"

namespace {

static bool WriteIdFile(const char* path, const char id[65]) {
    if (!path || !id) {
        DbgPrint("[iPhoneTether360:LICENSEID] write failed | operation=WriteIdFile arguments status=unavailable\n");
        return false;
    }
    it360_platform::FileHandle file = it360_platform::OpenTruncate(path);
    if (!file) {
        DbgPrint("[iPhoneTether360:LICENSEID] write failed | operation=CreateFileA result=NULL status=unavailable\n");
        return false;
    }
    if (!it360_platform::WriteAll(file, id, 64u)) {
        DbgPrint("[iPhoneTether360:LICENSEID] write failed | operation=WriteFile(ID) result=FALSE status=unavailable\n");
        it360_platform::CloseFile(file);
        return false;
    }
    if (!it360_platform::WriteAll(file, "\r\n", 2u)) {
        DbgPrint("[iPhoneTether360:LICENSEID] write failed | operation=WriteFile(CRLF) result=FALSE status=unavailable\n");
        it360_platform::CloseFile(file);
        return false;
    }
    it360_platform::CloseFile(file);
    return true;
}

} // namespace

int main() {
    char app_directory[512];
    char output_path[560];
    char id[65];
    id[0] = 0;

    it360_platform::ResolveStatus path_diagnostic;
    if (!it360_app_path::ResolveAppDirectory(app_directory, sizeof(app_directory), &path_diagnostic)) {
        if (path_diagnostic.has_status) {
            DbgPrint("[iPhoneTether360:LICENSEID] path resolve failed | operation=%s NTSTATUS=0x%08x\n",
                     it360_platform::ResolveOperationName(path_diagnostic.operation),
                     static_cast<unsigned>(path_diagnostic.status));
        } else {
            DbgPrint("[iPhoneTether360:LICENSEID] path resolve failed | operation=%s status=unavailable\n",
                     it360_platform::ResolveOperationName(path_diagnostic.operation));
        }
        it360_notify::Show("iPhoneTether360: LicenseID path failed");
        return 1;
    }
    if (!it360_app_path::Join(app_directory, "LicenseID.txt", output_path, sizeof(output_path))) {
        DbgPrint("[iPhoneTether360:LICENSEID] path build failed | operation=Join(LicenseID.txt) status=unavailable\n");
        it360_notify::Show("iPhoneTether360: LicenseID path failed");
        return 1;
    }

    it360_license::CpuKeyDiagnostic cpu_diagnostic;
    if (!it360_license::DeriveLicenceId(id, &cpu_diagnostic)) {
        if (cpu_diagnostic.failure == it360_license::CpuKeyFailureResolveExpansionCall &&
            cpu_diagnostic.resolve.has_status) {
            DbgPrint("[iPhoneTether360:LICENSEID] CPU key access unavailable | operation=%s/%s NTSTATUS=0x%08x\n",
                     it360_license::CpuKeyFailureName(cpu_diagnostic.failure),
                     it360_platform::ResolveOperationName(cpu_diagnostic.resolve.operation),
                     static_cast<unsigned>(cpu_diagnostic.resolve.status));
        } else {
            DbgPrint("[iPhoneTether360:LICENSEID] CPU key access unavailable | operation=%s status=unavailable\n",
                     it360_license::CpuKeyFailureName(cpu_diagnostic.failure));
        }
        it360_notify::Show("iPhoneTether360: CPU key access unavailable");
        return 1;
    }

    if (!WriteIdFile(output_path, id)) {
        it360_license::SecureZero(id, sizeof(id));
        it360_notify::Show("iPhoneTether360: LicenseID.txt write failed");
        return 1;
    }

    char display[96];
    static const char prefix[] = "License ID: ";
    unsigned p = 0;
    for (unsigned i = 0; i < sizeof(prefix) - 1u; ++i) display[p++] = prefix[i];
    for (unsigned i = 0; i < 64u; ++i) display[p++] = id[i];
    display[p] = 0;

    it360_notify::Show("iPhoneTether360 License ID");
    it360_notify::Show(display);
    it360_notify::Show("Saved to LicenseID.txt");

    it360_license::SecureZero(id, sizeof(id));
    it360_license::SecureZero(display, sizeof(display));
    return 0;
}
