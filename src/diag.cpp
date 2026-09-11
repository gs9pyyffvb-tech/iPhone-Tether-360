#include "platform/xbox_platform.h"
#include "early_trace.h"
#include "shared/notify.h"
#include "shared/version.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "diag.h"

namespace it360_diag {
namespace {

static const unsigned kLineMax = 512;
static const unsigned kQueueDepth = 256;
static const unsigned kNotifyMax = 160;
static const unsigned kNotifyDepth = 16;
static const uint64_t kNotifyGapMs = 2500ULL;

struct LogSlot { char text[kLineMax]; };
struct NotifySlot {
    char text[kNotifyMax];
    unsigned phoneConnection;
    unsigned kind;
};

static LogSlot gLogQueue[kQueueDepth];
static unsigned gLogHead = 0, gLogTail = 0, gLogCount = 0;
static NotifySlot gNotifyQueue[kNotifyDepth];
static unsigned gNotifyHead = 0, gNotifyTail = 0, gNotifyCount = 0;
static volatile LONG gQueueLock = 0;
static volatile LONG gRunning = 0;
static volatile LONG gStarted = 0;
static volatile LONG gDroppedLogs = 0;
static volatile LONG gPersistFailures = 0;
static volatile LONG gActivePhoneConnection = 0;
static uint64_t gLogSessionStartMs = 0;
static uint64_t gNextNotifyMs = 0;
static unsigned gFallbackNotifyTicks = 0;
static uint32_t gPhoneNoticeSeenMask = 0;

static bool TryLock() {
    for (unsigned i = 0; i < 256; ++i) {
        if (it360_platform::AtomicCompareExchange(&gQueueLock, 1, 0) == 0) return true;
    }
    return false;
}

static void Unlock() { it360_platform::AtomicExchange(&gQueueLock, 0); }

static bool PersistRuntime(const char* text, size_t length) {
    if (!text || !length) return true;
    const char* path = it360_early_trace::RuntimeLogPath();
    if (!path || !path[0]) {
        it360_platform::AtomicIncrement(&gPersistFailures);
        return false;
    }

    it360_platform::FileHandle file = it360_platform::OpenAppend(path);
    if (!file) {
        it360_platform::AtomicIncrement(&gPersistFailures);
        return false;
    }

    const bool ok = it360_platform::WriteAll(file, text, length);
    it360_platform::CloseFile(file);
    if (!ok) it360_platform::AtomicIncrement(&gPersistFailures);
    return ok;
}

static void WriteRuntimeLine(const char* text) {
    if (!text || !text[0]) return;

    uint64_t elapsed = 0;
    const uint64_t now = it360_platform::MonotonicMs();
    if (now && gLogSessionStartMs && now >= gLogSessionStartMs) elapsed = now - gLogSessionStartMs;

    char prefix[48];
    snprintf(prefix, sizeof(prefix) - 1u, "[+%010llu ms] ", static_cast<unsigned long long>(elapsed));
    prefix[sizeof(prefix) - 1u] = 0;

    const bool okPrefix = PersistRuntime(prefix, strlen(prefix));
    const bool okText = PersistRuntime(text, strlen(text));
    if (!okPrefix || !okText) {
        DbgPrint("[iPhoneTether360:LOGGER] Log.txt persistence FAILED total_failures=%d\n",
                 static_cast<int>(it360_platform::AtomicCompareExchange(&gPersistFailures, 0, 0)));
    }
}

static bool PopLog(char* out, unsigned capacity) {
    if (!out || capacity < 2 || !TryLock()) return false;
    if (!gLogCount) { Unlock(); return false; }
    strncpy(out, gLogQueue[gLogHead].text, capacity - 1u);
    out[capacity - 1u] = 0;
    gLogQueue[gLogHead].text[0] = 0;
    gLogHead = (gLogHead + 1u) % kQueueDepth;
    --gLogCount;
    Unlock();
    return true;
}

static bool NotifyDue() {
    const uint64_t now = it360_platform::MonotonicMs();
    if (now) return gNextNotifyMs == 0 || now >= gNextNotifyMs;
    return gFallbackNotifyTicks == 0;
}

static void MarkNotifyShown(const char* text) {
    const uint64_t now = it360_platform::MonotonicMs();
    if (now) gNextNotifyMs = now + kNotifyGapMs;
    else gFallbackNotifyTicks = static_cast<unsigned>(kNotifyGapMs / 10ULL);
    (void)text;
}

static bool PopNotify(char* out, unsigned capacity) {
    if (!out || capacity < 2 || !TryLock()) return false;

    while (gNotifyCount) {
        NotifySlot* slot = &gNotifyQueue[gNotifyHead];
        const unsigned active = static_cast<unsigned>(
            it360_platform::AtomicCompareExchange(&gActivePhoneConnection, 0, 0));
        const bool stale = slot->phoneConnection != 0u && slot->phoneConnection != active;
        if (stale) {
            slot->text[0] = 0;
            slot->phoneConnection = 0;
            slot->kind = 0;
            gNotifyHead = (gNotifyHead + 1u) % kNotifyDepth;
            --gNotifyCount;
            continue;
        }

        strncpy(out, slot->text, capacity - 1u);
        out[capacity - 1u] = 0;
        slot->text[0] = 0;
        slot->phoneConnection = 0;
        slot->kind = 0;
        gNotifyHead = (gNotifyHead + 1u) % kNotifyDepth;
        --gNotifyCount;
        Unlock();
        return true;
    }

    Unlock();
    return false;
}

static void RemovePhoneNotificationsUnlocked(unsigned connection_id) {
    if (!connection_id || !gNotifyCount) return;
    NotifySlot kept[kNotifyDepth];
    unsigned keptCount = 0;
    for (unsigned i = 0; i < gNotifyCount; ++i) {
        const unsigned index = (gNotifyHead + i) % kNotifyDepth;
        const NotifySlot& slot = gNotifyQueue[index];
        if (slot.phoneConnection == connection_id) continue;
        kept[keptCount++] = slot;
    }
    memset(gNotifyQueue, 0, sizeof(gNotifyQueue));
    for (unsigned i = 0; i < keptCount; ++i) gNotifyQueue[i] = kept[i];
    gNotifyHead = 0;
    gNotifyTail = keptCount % kNotifyDepth;
    gNotifyCount = keptCount;
}

static bool IsProgressKind(unsigned kind) {
    return kind == PhoneNoticeCheckingData ||
           kind == PhoneNoticeDataWorking ||
           kind == PhoneNoticeDataNotWorking ||
           kind == PhoneNoticeComplete ||
           kind == PhoneNoticeReconnecting;
}

static void RemovePhoneProgressUnlocked(unsigned connection_id) {
    if (!connection_id || !gNotifyCount) return;
    NotifySlot kept[kNotifyDepth];
    unsigned keptCount = 0;
    for (unsigned i = 0; i < gNotifyCount; ++i) {
        const unsigned index = (gNotifyHead + i) % kNotifyDepth;
        const NotifySlot& slot = gNotifyQueue[index];
        if (slot.phoneConnection == connection_id && IsProgressKind(slot.kind)) continue;
        kept[keptCount++] = slot;
    }
    memset(gNotifyQueue, 0, sizeof(gNotifyQueue));
    for (unsigned i = 0; i < keptCount; ++i) gNotifyQueue[i] = kept[i];
    gNotifyHead = 0;
    gNotifyTail = keptCount % kNotifyDepth;
    gNotifyCount = keptCount;
}

static bool DuplicateQueuedUnlocked(unsigned phone_connection, unsigned kind, const char* text) {
    for (unsigned i = 0; i < gNotifyCount; ++i) {
        const unsigned index = (gNotifyHead + i) % kNotifyDepth;
        const NotifySlot& slot = gNotifyQueue[index];
        if (phone_connection && slot.phoneConnection == phone_connection && slot.kind == kind) return true;
        if (text && slot.text[0] && strcmp(slot.text, text) == 0) return true;
    }
    return false;
}

static void QueueNotification(unsigned phone_connection, unsigned kind, const char* text) {
    if (!text || !text[0]) return;
    Log("[iPhoneTether360:NOTIFY] %s\n", text);

    if (!TryLock()) return;
    if (DuplicateQueuedUnlocked(phone_connection, kind, text)) {
        Unlock();
        return;
    }

    if (gNotifyCount == kNotifyDepth) {
        gNotifyQueue[gNotifyHead].text[0] = 0;
        gNotifyHead = (gNotifyHead + 1u) % kNotifyDepth;
        --gNotifyCount;
    }

    NotifySlot* slot = &gNotifyQueue[gNotifyTail];
    strncpy(slot->text, text, kNotifyMax - 1u);
    slot->text[kNotifyMax - 1u] = 0;
    slot->phoneConnection = phone_connection;
    slot->kind = kind;
    gNotifyTail = (gNotifyTail + 1u) % kNotifyDepth;
    ++gNotifyCount;
    Unlock();
}

static void SystemNotifyTrace(const char* text) {
    if (!text || !text[0]) return;
    Log("[iPhoneTether360:NOTIFY-SYSTEM] %s\r\n", text);
}

static void ReportDroppedLogs() {
    const LONG dropped = it360_platform::AtomicExchange(&gDroppedLogs, 0);
    if (dropped <= 0) return;
    char warning[kLineMax];
    snprintf(warning, sizeof(warning) - 1u,
             "[iPhoneTether360:LOGGER] WARNING: %d queued log messages were dropped\r\n",
             static_cast<int>(dropped));
    warning[sizeof(warning) - 1u] = 0;
    DbgPrint("%s", warning);
    WriteRuntimeLine(warning);
}

static uint32_t Worker(void*) {
    it360_early_trace::WriteLiteral("LOGGER 20: Log.txt worker entered");
    char banner[128];
    snprintf(banner, sizeof(banner) - 1u,
             "========== iPhoneTether360 %s runtime log ==========\r\n",
             IT360_VERSION_STRING);
    banner[sizeof(banner) - 1u] = 0;
    WriteRuntimeLine(banner);

    while (it360_platform::AtomicCompareExchange(&gRunning, 0, 0) != 0) {
        char line[kLineMax];
        while (PopLog(line, sizeof(line))) WriteRuntimeLine(line);
        ReportDroppedLogs();

        if (NotifyDue()) {
            char note[kNotifyMax];
            if (PopNotify(note, sizeof(note))) {
                it360_notify::ShowSystem(note);
                MarkNotifyShown(note);
            }
        }

        if (!it360_platform::MonotonicMs() && gFallbackNotifyTicks) --gFallbackNotifyTicks;
        it360_platform::SleepMs(10);
    }

    char line[kLineMax];
    while (PopLog(line, sizeof(line))) WriteRuntimeLine(line);
    ReportDroppedLogs();
    it360_early_trace::WriteLiteral("LOGGER 99: Log.txt worker exiting");
    return 0;
}

} // namespace

void BootCheckpoint(const char* format, ...) {
    if (!format) return;
    char message[256];
    va_list ap;
    va_start(ap, format);
    vsnprintf(message, sizeof(message) - 1u, format, ap);
    va_end(ap);
    message[sizeof(message) - 1u] = 0;
    DbgPrint("[iPhoneTether360:BOOT] %s\n", message);

    char line[352];
    snprintf(line, sizeof(line) - 1u, "[iPhoneTether360:BOOT] %s", message);
    line[sizeof(line) - 1u] = 0;
    if (!it360_early_trace::WriteLiteral(line)) {
        DbgPrint("[iPhoneTether360:LOGGER] Boot.log checkpoint persistence FAILED status=%08x\n",
                 static_cast<unsigned>(it360_early_trace::LastStatus()));
    }
}

void Init() {
    BootCheckpoint("LOGGER 10: diagnostics Init entered; boot_ready=%u boot_failures=%u boot_status=%08x",
                   it360_early_trace::PersistentReady() ? 1u : 0u,
                   static_cast<unsigned>(it360_early_trace::FailureCount()),
                   static_cast<unsigned>(it360_early_trace::LastStatus()));

    if (it360_platform::AtomicCompareExchange(&gStarted, 1, 0) != 0) {
        BootCheckpoint("LOGGER 11: diagnostics already initialized");
        return;
    }

    it360_notify::SetSystemTrace(&SystemNotifyTrace);
    gLogSessionStartMs = it360_platform::MonotonicMs();
    gNextNotifyMs = 0;
    gFallbackNotifyTicks = 0;
    gPhoneNoticeSeenMask = 0;
    it360_platform::AtomicExchange(&gActivePhoneConnection, 0);

    const char* runtimePath = it360_early_trace::RuntimeLogPath();
    if (runtimePath && runtimePath[0]) {
        it360_platform::FileHandle file = it360_platform::OpenTruncate(runtimePath);
        if (file) {
            it360_platform::CloseFile(file);
            BootCheckpoint("LOGGER 11A: Log.txt reset for new Core session");
        } else {
            BootCheckpoint("LOGGER 11A: Log.txt reset failed; continuing");
        }
    }

    it360_platform::AtomicExchange(&gRunning, 1);
    BootCheckpoint("LOGGER 12: creating Log.txt persistence worker");

    it360_platform::ThreadStartStatus worker_status;
    if (!it360_platform::StartDetachedThread(Worker, 0, &worker_status)) {
        it360_platform::AtomicExchange(&gRunning, 0);
        it360_platform::AtomicExchange(&gStarted, 0);
        if (worker_status.has_status)
            BootCheckpoint("LOGGER 13 FAILED: operation=%s NTSTATUS=0x%08x",
                           it360_platform::ThreadStartOperationName(worker_status.operation),
                           static_cast<unsigned>(worker_status.status));
        else
            BootCheckpoint("LOGGER 13 FAILED: operation=%s status=unavailable",
                           it360_platform::ThreadStartOperationName(worker_status.operation));
        return;
    }

    if (worker_status.close_status_valid && it360_platform::FailedStatus(worker_status.close_status))
        BootCheckpoint("LOGGER 13 WARNING: NtClose(thread handle) NTSTATUS=0x%08x",
                       static_cast<unsigned>(worker_status.close_status));
    BootCheckpoint("LOGGER 13: persistence worker creation returned success");
}

void Shutdown() {
    BootCheckpoint("LOGGER 90: diagnostics Shutdown entered");
    if (it360_platform::AtomicCompareExchange(&gStarted, 0, 0) == 0) return;
    it360_platform::AtomicExchange(&gRunning, 0);
    it360_platform::AtomicExchange(&gStarted, 0);
}

void Log(const char* format, ...) {
    if (!format) return;
    char line[kLineMax];
    va_list ap;
    va_start(ap, format);
    const int n = vsnprintf(line, sizeof(line) - 1u, format, ap);
    va_end(ap);
    line[sizeof(line) - 1u] = 0;
    if (n < 0) line[0] = 0;
    DbgPrint("%s", line);
    if (!line[0]) return;

    if (!TryLock()) { it360_platform::AtomicIncrement(&gDroppedLogs); return; }
    if (gLogCount == kQueueDepth) {
        gLogHead = (gLogHead + 1u) % kQueueDepth;
        --gLogCount;
        it360_platform::AtomicIncrement(&gDroppedLogs);
    }
    strncpy(gLogQueue[gLogTail].text, line, kLineMax - 1u);
    gLogQueue[gLogTail].text[kLineMax - 1u] = 0;
    gLogTail = (gLogTail + 1u) % kQueueDepth;
    ++gLogCount;
    Unlock();
}

void Notify(const char* text) {
    QueueNotification(0u, 0u, text);
}

void BeginPhoneNotifications(unsigned connection_id) {
    if (!connection_id) return;
    if (!TryLock()) {
        it360_platform::AtomicExchange(&gActivePhoneConnection, static_cast<LONG>(connection_id));
        return;
    }

    const unsigned previous = static_cast<unsigned>(
        it360_platform::AtomicCompareExchange(&gActivePhoneConnection, 0, 0));
    if (previous && previous != connection_id) RemovePhoneNotificationsUnlocked(previous);
    gPhoneNoticeSeenMask = 0;
    it360_platform::AtomicExchange(&gActivePhoneConnection, static_cast<LONG>(connection_id));
    Unlock();
}

void EndPhoneNotifications(unsigned connection_id) {
    if (!connection_id) return;
    if (!TryLock()) {
        if (static_cast<unsigned>(it360_platform::AtomicCompareExchange(&gActivePhoneConnection, 0, 0)) == connection_id)
            it360_platform::AtomicExchange(&gActivePhoneConnection, 0);
        return;
    }

    RemovePhoneNotificationsUnlocked(connection_id);
    if (static_cast<unsigned>(it360_platform::AtomicCompareExchange(&gActivePhoneConnection, 0, 0)) == connection_id) {
        gPhoneNoticeSeenMask = 0;
        it360_platform::AtomicExchange(&gActivePhoneConnection, 0);
    }
    Unlock();
}

void ResetPhoneProgressNotifications(unsigned connection_id) {
    if (!connection_id || !TryLock()) return;
    if (static_cast<unsigned>(it360_platform::AtomicCompareExchange(&gActivePhoneConnection, 0, 0)) == connection_id) {
        const uint32_t progressMask =
            (1u << (PhoneNoticeCheckingData - 1)) |
            (1u << (PhoneNoticeDataWorking - 1)) |
            (1u << (PhoneNoticeDataNotWorking - 1)) |
            (1u << (PhoneNoticeComplete - 1)) |
            (1u << (PhoneNoticeReconnecting - 1));
        gPhoneNoticeSeenMask &= ~progressMask;
        RemovePhoneProgressUnlocked(connection_id);
    }
    Unlock();
}

void NotifyPhone(unsigned connection_id, PhoneNoticeKind kind, const char* text) {
    if (!connection_id || !text || !text[0]) return;
    if (kind < PhoneNoticeDetected || kind > PhoneNoticeReconnecting) return;
    if (!TryLock()) return;
    if (static_cast<unsigned>(it360_platform::AtomicCompareExchange(&gActivePhoneConnection, 0, 0)) != connection_id) {
        Unlock();
        return;
    }

    const uint32_t bit = 1u << (static_cast<unsigned>(kind) - 1u);
    if (kind == PhoneNoticeDataWorking && gNotifyCount) {
        NotifySlot kept[kNotifyDepth];
        unsigned keptCount = 0;
        for (unsigned i = 0; i < gNotifyCount; ++i) {
            const unsigned index = (gNotifyHead + i) % kNotifyDepth;
            const NotifySlot& queued = gNotifyQueue[index];
            if (queued.phoneConnection == connection_id && queued.kind == PhoneNoticeDataNotWorking) continue;
            kept[keptCount++] = queued;
        }
        memset(gNotifyQueue, 0, sizeof(gNotifyQueue));
        for (unsigned i = 0; i < keptCount; ++i) gNotifyQueue[i] = kept[i];
        gNotifyHead = 0;
        gNotifyTail = keptCount % kNotifyDepth;
        gNotifyCount = keptCount;
        gPhoneNoticeSeenMask &= ~(1u << (PhoneNoticeDataNotWorking - 1));
    }

    if ((gPhoneNoticeSeenMask & bit) != 0u ||
        DuplicateQueuedUnlocked(connection_id, static_cast<unsigned>(kind), text)) {
        Unlock();
        return;
    }

    if (gNotifyCount == kNotifyDepth) {
        gNotifyQueue[gNotifyHead].text[0] = 0;
        gNotifyHead = (gNotifyHead + 1u) % kNotifyDepth;
        --gNotifyCount;
    }
    NotifySlot* slot = &gNotifyQueue[gNotifyTail];
    strncpy(slot->text, text, kNotifyMax - 1u);
    slot->text[kNotifyMax - 1u] = 0;
    slot->phoneConnection = connection_id;
    slot->kind = static_cast<unsigned>(kind);
    gNotifyTail = (gNotifyTail + 1u) % kNotifyDepth;
    ++gNotifyCount;
    gPhoneNoticeSeenMask |= bit;
    Unlock();
    Log("[iPhoneTether360:NOTIFY] %s\n", text);
}

const char* LogPath() { return it360_early_trace::RuntimeLogPath(); }

} // namespace it360_diag
