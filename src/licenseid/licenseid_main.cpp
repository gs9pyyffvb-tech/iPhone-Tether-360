#include "platform/xbox_platform.h"
#include "shared/notify.h"
#include "license/license_id.h"
#include "license/cpu_key.h"

namespace {

static const char kLogPath[] = "game:\\LicenseID.log";
static const char kOutputPath[] = "game:\\LicenseID.txt";

static size_t TextLength(const char* text) {
    if (!text) return 0;
    size_t n = 0;
    while (text[n]) ++n;
    return n;
}

static bool WriteRecord(it360_platform::FileHandle file, const char* text) {
    if (!file || !text) return false;
    const size_t length = TextLength(text);
    if (length && !it360_platform::WriteAll(file, text, length)) return false;
    return it360_platform::WriteAll(file, "\r\n", 2u);
}

static bool BeginLog() {
    it360_platform::FileHandle file = it360_platform::OpenTruncate(kLogPath);
    if (!file) return false;
    const bool ok = WriteRecord(file, "LIC00 | LicenseID main entered");
    it360_platform::CloseFile(file);
    return ok;
}

static void LogLine(const char* text) {
    if (!text) return;
    it360_platform::FileHandle file = it360_platform::OpenAppend(kLogPath);
    if (!file) {
        DbgPrint("[iPhoneTether360:LICENSEID] could not append game:\\LicenseID.log\n");
        return;
    }
    if (!WriteRecord(file, text))
        DbgPrint("[iPhoneTether360:LICENSEID] LicenseID.log write failed\n");
    it360_platform::CloseFile(file);
}

static char HexDigit(unsigned value) {
    return value < 10u ? static_cast<char>('0' + value)
                       : static_cast<char>('A' + value - 10u);
}

static void LogHex32(const char* prefix, uint32_t value) {
    char line[160];
    size_t p = 0;
    if (prefix) {
        while (prefix[p] && p + 11u < sizeof(line)) {
            line[p] = prefix[p];
            ++p;
        }
    }
    if (p + 10u >= sizeof(line)) return;
    line[p++] = '0';
    line[p++] = 'x';
    for (int shift = 28; shift >= 0; shift -= 4)
        line[p++] = HexDigit((value >> shift) & 0xFu);
    line[p] = 0;
    LogLine(line);
}

static void LogText(const char* prefix, const char* value) {
    char line[192];
    size_t p = 0;
    if (prefix) {
        while (*prefix && p + 1u < sizeof(line)) line[p++] = *prefix++;
    }
    if (value) {
        while (*value && p + 1u < sizeof(line)) line[p++] = *value++;
    }
    line[p] = 0;
    LogLine(line);
}

static int TerminateTitle(int result, const char* final_line) {
    if (final_line) LogLine(final_line);
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    XamLoaderTerminateTitle();
#endif
    return result;
}

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

static int FailWithNotification(const char* log_line, const char* notification) {
    LogLine(log_line);
    const bool shown = it360_notify::ShowTitle(notification);
    LogLine(shown
        ? "LICFAIL | Failure notification returned success"
        : "LICFAIL | Failure notification returned failure");
    return TerminateTitle(1, "LIC07 | Terminating LicenseID title after failure");
}

} // namespace

int main() {
    if (!BeginLog()) {
        DbgPrint("[iPhoneTether360:LICENSEID] could not create game:\\LicenseID.log\n");
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
        XamLoaderTerminateTitle();
#endif
        return 1;
    }

    char id[65];
    id[0] = 0;

    LogLine("LIC01 | About to derive licence ID from CPU key");
    it360_license::CpuKeyDiagnostic cpu_diagnostic;
    if (!it360_license::DeriveLicenceId(id, &cpu_diagnostic)) {
        LogLine("LIC02 | Licence ID derivation failed");
        LogText("LIC02A | CPU key failure = ",
                it360_license::CpuKeyFailureName(cpu_diagnostic.failure));
        if (cpu_diagnostic.resolve.has_status) {
            LogText("LIC02B | Resolve operation = ",
                    it360_platform::ResolveOperationName(cpu_diagnostic.resolve.operation));
            LogHex32("LIC02C | Resolve NTSTATUS = ",
                     static_cast<uint32_t>(cpu_diagnostic.resolve.status));
        } else {
            LogLine("LIC02B | Resolve NTSTATUS unavailable");
        }

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
        it360_license::SecureZero(id, sizeof(id));
        return FailWithNotification("LIC02D | CPU key access unavailable",
                                    "iPhoneTether360: CPU key access unavailable");
    }

    LogLine("LIC02 | Licence ID derivation succeeded");
    LogLine("LIC03 | Writing game:\\LicenseID.txt");
    if (!WriteIdFile(kOutputPath, id)) {
        it360_license::SecureZero(id, sizeof(id));
        return FailWithNotification("LIC04 | LicenseID.txt write failed",
                                    "iPhoneTether360: LicenseID.txt write failed");
    }
    LogLine("LIC04 | LicenseID.txt written successfully");

    char display[96];
    static const char prefix[] = "License ID: ";
    unsigned p = 0;
    for (unsigned i = 0; i < sizeof(prefix) - 1u; ++i) display[p++] = prefix[i];
    for (unsigned i = 0; i < 64u; ++i) display[p++] = id[i];
    display[p] = 0;

    LogLine("LIC05 | About to show LicenseID notifications");
    const bool title_shown = it360_notify::ShowTitle("iPhoneTether360 License ID");
    it360_platform::SleepMs(2500u);
    const bool id_shown = it360_notify::ShowTitle(display);
    it360_platform::SleepMs(2500u);
    const bool saved_shown = it360_notify::ShowTitle("Saved to LicenseID.txt");
    LogLine(title_shown && id_shown && saved_shown
        ? "LIC06 | LicenseID notifications returned success"
        : "LIC06 | One or more LicenseID notifications returned failure");

    it360_license::SecureZero(id, sizeof(id));
    it360_license::SecureZero(display, sizeof(display));
    return TerminateTitle(0, "LIC07 | Terminating LicenseID title");
}
