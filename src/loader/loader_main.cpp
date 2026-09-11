#include "platform/xbox_platform.h"
#include "shared/notify.h"
#include "shared/version.h"

namespace {

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
static const DWORD kResidentSystemDllFlags = 0x0000000Au;
#endif
static const char kBootLogPath[] = "game:\\Boot.log";
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
static char kCorePath[] = "game:\\Core.xex";
#endif

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
    it360_platform::FileHandle file = it360_platform::OpenTruncate(kBootLogPath);
    if (!file) return false;

    bool ok = true;
    ok = WriteRecord(file, "============================================================") && ok;
    ok = WriteRecord(file, IT360_PRODUCT_VERSION " Loader session") && ok;
    ok = WriteRecord(file, "============================================================") && ok;
    ok = WriteRecord(file, "LOADER00 | Loader main entered") && ok;
    it360_platform::CloseFile(file);
    return ok;
}

static void LogLine(const char* text) {
    if (!text) return;
    it360_platform::FileHandle file = it360_platform::OpenAppend(kBootLogPath);
    if (!file) {
        DbgPrint("[iPhoneTether360:LOADER] could not append game:\\Boot.log\n");
        return;
    }
    if (!WriteRecord(file, text))
        DbgPrint("[iPhoneTether360:LOADER] Boot.log write failed\n");
    it360_platform::CloseFile(file);
}

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
static char HexDigit(unsigned value) {
    return value < 10u ? static_cast<char>('0' + value)
                       : static_cast<char>('A' + value - 10u);
}

static void LogHex32(const char* prefix, uint32_t value) {
    char line[128];
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

#endif

static int TerminateTitle(int result, const char* final_line) {
    if (final_line) LogLine(final_line);
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    XamLoaderTerminateTitle();
#endif
    return result;
}

static bool CoreAlreadyResident() {
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    HMODULE module = 0;
    NTSTATUS status = XexGetModuleHandle("Core.xex", &module);
    LogHex32("LOADER03A | XexGetModuleHandle(Core.xex) NTSTATUS = ",
             static_cast<uint32_t>(status));
    if (!it360_platform::FailedStatus(status) && module) return true;
    if (!it360_platform::FailedStatus(status) && !module)
        LogLine("LOADER03A | XexGetModuleHandle(Core.xex) returned success with NULL module");

    module = 0;
    status = XexGetModuleHandle("Core.exe", &module);
    LogHex32("LOADER03B | XexGetModuleHandle(Core.exe) NTSTATUS = ",
             static_cast<uint32_t>(status));
    if (!it360_platform::FailedStatus(status) && module) return true;
    if (!it360_platform::FailedStatus(status) && !module)
        LogLine("LOADER03B | XexGetModuleHandle(Core.exe) returned success with NULL module");

    module = 0;
    status = XexGetModuleHandle(kCorePath, &module);
    LogHex32("LOADER03C | XexGetModuleHandle(game:\\Core.xex) NTSTATUS = ",
             static_cast<uint32_t>(status));
    if (!it360_platform::FailedStatus(status) && module) return true;
    if (!it360_platform::FailedStatus(status) && !module)
        LogLine("LOADER03C | XexGetModuleHandle(game:\\Core.xex) returned success with NULL module");
#endif
    return false;
}

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
static int Fail(const char* log_line, const char* notification) {
    LogLine(log_line);
    LogLine("LOADER07A | About to show failure notification");
    const bool shown = it360_notify::Show(notification);
    LogLine(shown
        ? "LOADER07B | Failure notification returned success"
        : "LOADER07B | Failure notification returned failure");
    return TerminateTitle(1, "LOADER08 | Terminating Loader title after failure");
}
#endif

} // namespace

int main() {
    if (!BeginLog()) {
        DbgPrint("[iPhoneTether360:LOADER] could not create game:\\Boot.log\n");
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
        XamLoaderTerminateTitle();
#endif
        return 1;
    }

    LogLine("LOADER01 | About to show startup notification");
    const bool startup_shown = it360_notify::Show("Starting iPhoneTether360...");
    LogLine(startup_shown
        ? "LOADER02 | Startup notification returned success"
        : "LOADER02 | Startup notification returned failure");

    LogLine("LOADER03 | Checking for resident Core");
    if (CoreAlreadyResident()) {
        LogLine("LOADER04 | Core already resident; duplicate load blocked");
        const bool shown = it360_notify::Show("iPhoneTether360 Already Running");
        LogLine(shown
            ? "LOADER04A | Already-running notification returned success"
            : "LOADER04A | Already-running notification returned failure");
        return TerminateTitle(0, "LOADER08 | Terminating Loader title and returning to Aurora");
    }

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    HMODULE core_module = 0;
    LogLine("LOADER04 | Core not resident");
    LogLine("LOADER05 | Calling XexLoadImage for Core.xex");
    LogLine("LOADER05A | Core path = game:\\Core.xex");

    const NTSTATUS status = XexLoadImage(
        kCorePath,
        kResidentSystemDllFlags,
        0u,
        &core_module
    );

    LogHex32("LOADER06 | XexLoadImage status = ", static_cast<uint32_t>(status));
    LogHex32("LOADER06A | Core module handle = ", it360_platform::Address32(core_module));

    if (it360_platform::FailedStatus(status) || !core_module) {
        if (!it360_platform::FailedStatus(status) && !core_module)
            LogLine("LOADER07 | XexLoadImage returned success with NULL module");
        return Fail("LOADER07 | Core load failed after XexLoadImage",
                    "iPhoneTether360: Core failed to load");
    }

    LogLine("LOADER07 | Core load succeeded");
    return TerminateTitle(0, "LOADER08 | Terminating Loader title and returning to Aurora");
#else
    return TerminateTitle(0, "LOADER04 | Host syntax path complete");
#endif
}
