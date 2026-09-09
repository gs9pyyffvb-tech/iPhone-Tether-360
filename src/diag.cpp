#include "platform/xbox_platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "diag.h"


namespace it360_diag {
namespace {

static const unsigned kLineMax = 512;
static const unsigned kQueueDepth = 64;
static const unsigned kNotifyMax = 128;
static const unsigned kNotifyDepth = 8;

struct LogSlot { char text[kLineMax]; };
struct NotifySlot { char text[kNotifyMax]; };

static LogSlot gLogQueue[kQueueDepth];
static unsigned gLogHead = 0;
static unsigned gLogTail = 0;
static unsigned gLogCount = 0;
static NotifySlot gNotifyQueue[kNotifyDepth];
static unsigned gNotifyHead = 0;
static unsigned gNotifyTail = 0;
static unsigned gNotifyCount = 0;
static volatile LONG gQueueLock = 0;
static volatile LONG gRunning = 0;
static volatile LONG gStarted = 0;
static it360_platform::FileHandle gLogFile = 0;
static char gLogPath[64] = {0};

#if defined(IT360_XBOX) && defined(IT360_ENABLE_XNOTIFY)
typedef void (*XNotifyQueueUIFn)(DWORD type, DWORD userIndex, ULONGLONG areas,
                                  const WCHAR* displayText, void* contextData);
static XNotifyQueueUIFn gXNotifyQueueUI = 0;
#endif

static bool TryLock() {
    // Logging must never stall a USB completion callback. If contention lasts
    // more than this very short bounded loop, DbgPrint still has the message and
    // only the persistent copy is dropped.
    for (unsigned i = 0; i < 128; ++i) {
        if (it360_platform::AtomicCompareExchange(&gQueueLock, 1, 0) == 0) return true;
    }
    return false;
}

static void Unlock() { it360_platform::AtomicExchange(&gQueueLock, 0); }

static void CloseLogFile() {
    if (gLogFile) {
        it360_platform::CloseFile(gLogFile);
        gLogFile = 0;
    }
    gLogPath[0] = 0;
}

static bool OpenLogFile() {
    if (gLogFile) return true;
    static const char* paths[] = {
        "Hdd1:\\iPhoneTether360.log",
        "Usb0:\\iPhoneTether360.log",
        "Usb1:\\iPhoneTether360.log",
        "Usb2:\\iPhoneTether360.log",
        "Usb3:\\iPhoneTether360.log"
    };
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        it360_platform::FileHandle h = it360_platform::OpenAppend(paths[i]);
        if (!h) continue;
        gLogFile = h;
        strncpy(gLogPath, paths[i], sizeof(gLogPath) - 1);
        gLogPath[sizeof(gLogPath) - 1] = 0;
        return true;
    }
    return false;
}

static void WriteLine(const char* text) {
    if (!text || !text[0] || !OpenLogFile()) return;
    const size_t n = strlen(text);
    if (!it360_platform::WriteAll(gLogFile, text, n)) CloseLogFile();
}

static bool PopLog(char* out, unsigned cap) {
    if (!out || cap < 2 || !TryLock()) return false;
    if (!gLogCount) { Unlock(); return false; }
    strncpy(out, gLogQueue[gLogHead].text, cap - 1);
    out[cap - 1] = 0;
    gLogQueue[gLogHead].text[0] = 0;
    gLogHead = (gLogHead + 1) % kQueueDepth;
    --gLogCount;
    Unlock();
    return true;
}

static bool PopNotify(char* out, unsigned cap) {
    if (!out || cap < 2 || !TryLock()) return false;
    if (!gNotifyCount) { Unlock(); return false; }
    strncpy(out, gNotifyQueue[gNotifyHead].text, cap - 1);
    out[cap - 1] = 0;
    gNotifyQueue[gNotifyHead].text[0] = 0;
    gNotifyHead = (gNotifyHead + 1) % kNotifyDepth;
    --gNotifyCount;
    Unlock();
    return true;
}

#if defined(IT360_XBOX) && defined(IT360_ENABLE_XNOTIFY)
static void ResolveNotify() {
    void* p = 0;
    // XNotifyQueueUI is xam.xex export ordinal 0x290 (656) on 17559.
    if (it360_platform::ResolveModuleOrdinal("xam.xex", 0x290, &p))
        gXNotifyQueueUI = reinterpret_cast<XNotifyQueueUIFn>(p);
}

static void ShowNotify(const char* text) {
    if (!gXNotifyQueueUI || !text || !text[0]) return;
    WCHAR w[kNotifyMax];
    unsigned i = 0;
    for (; i + 1 < kNotifyMax && text[i]; ++i)
        w[i] = (WCHAR)(unsigned char)text[i];
    w[i] = 0;
    // Type 23 is the generic/flashing-logo notification used for arbitrary text
    // on 17559. 0xFF addresses the active user; area 2 is the system UI area.
    gXNotifyQueueUI(23, 0xFF, 2, w, 0);
}
#else
static void ResolveNotify() {}
static void ShowNotify(const char*) {}
#endif

static uint32_t Worker(void*) {
    char line[kLineMax];
    char note[kNotifyMax];
    // Give dashboard storage mappings a brief chance to settle after plugin load.
    it360_platform::SleepMs(250);
    WriteLine("\r\n========== iPhoneTether360 diagnostic session ==========" "\r\n");
    while (it360_platform::AtomicCompareExchange(&gRunning, 0, 0) != 0) {
        bool didWork = false;
        while (PopLog(line, sizeof(line))) { WriteLine(line); didWork = true; }
        if (PopNotify(note, sizeof(note))) { ShowNotify(note); didWork = true; }
        if (!didWork) it360_platform::SleepMs(100);
    }
    while (PopLog(line, sizeof(line))) WriteLine(line);
    CloseLogFile();
    return 0;
}

} // namespace

void BootCheckpoint(const char* format, ...) {
    if (!format) return;

    char message[256];

    va_list ap;
    va_start(ap, format);
    vsnprintf(message, sizeof(message) - 1, format, ap);
    va_end(ap);

    message[sizeof(message) - 1] = 0;

    DbgPrint("[iPhoneTether360:BOOT] %s\n", message);

    char line[320];
    snprintf(
        line,
        sizeof(line) - 1,
        "[iPhoneTether360:BOOT] %s\r\n",
        message
    );
    line[sizeof(line) - 1] = 0;

    static const char* paths[] = {
        "Hdd1:\\iPhoneTether360.boot.log",
        "Usb0:\\iPhoneTether360.boot.log",
        "Usb1:\\iPhoneTether360.boot.log",
        "Usb2:\\iPhoneTether360.boot.log",
        "Usb3:\\iPhoneTether360.boot.log"
    };

    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        it360_platform::FileHandle file =
            it360_platform::OpenAppend(paths[i]);

        if (!file)
            continue;

        const bool ok = it360_platform::WriteAll(
            file,
            line,
            strlen(line)
        );

        it360_platform::CloseFile(file);

        if (ok)
            return;
    }
}

void Init() {
    if (it360_platform::AtomicCompareExchange(&gStarted, 1, 0) != 0) return;
    ResolveNotify();
    it360_platform::AtomicExchange(&gRunning, 1);
    if (!it360_platform::StartDetachedThread(Worker, 0)) {
        it360_platform::AtomicExchange(&gRunning, 0);
        it360_platform::AtomicExchange(&gStarted, 0);
        DbgPrint("[iPhoneTether360:B9C-OXC] WARNING diagnostics worker could not start\n");
        return;
    }
}

void Shutdown() {
    if (it360_platform::AtomicCompareExchange(&gStarted, 0, 0) == 0) return;
    it360_platform::AtomicExchange(&gRunning, 0);
    it360_platform::AtomicExchange(&gStarted, 0);
}

void Log(const char* format, ...) {
    if (!format) return;
    char line[kLineMax];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(line, sizeof(line) - 1, format, ap);
    va_end(ap);
    line[sizeof(line) - 1] = 0;
    if (n < 0) line[sizeof(line) - 1] = 0;

    // Retain the original debugger output regardless of persistent-log state.
    DbgPrint("%s", line);

    if (!TryLock()) return;
    if (gLogCount == kQueueDepth) {
        // Drop oldest diagnostic line rather than blocking USB callbacks.
        gLogHead = (gLogHead + 1) % kQueueDepth;
        --gLogCount;
    }
    strncpy(gLogQueue[gLogTail].text, line, kLineMax - 1);
    gLogQueue[gLogTail].text[kLineMax - 1] = 0;
    gLogTail = (gLogTail + 1) % kQueueDepth;
    ++gLogCount;
    Unlock();
}

void Notify(const char* text) {
    if (!text || !text[0] || !TryLock()) return;
    if (gNotifyCount == kNotifyDepth) {
        gNotifyHead = (gNotifyHead + 1) % kNotifyDepth;
        --gNotifyCount;
    }
    strncpy(gNotifyQueue[gNotifyTail].text, text, kNotifyMax - 1);
    gNotifyQueue[gNotifyTail].text[kNotifyMax - 1] = 0;
    gNotifyTail = (gNotifyTail + 1) % kNotifyDepth;
    ++gNotifyCount;
    Unlock();
}

const char* LogPath() { return gLogPath; }

} // namespace it360_diag
