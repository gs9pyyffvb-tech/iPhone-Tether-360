#include "shared/notify.h"
#include "platform/xbox_platform.h"

namespace it360_notify {
namespace {

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)

static volatile LONG gSystemThreadBusy = 0;
static WCHAR gSystemThreadText[160];
static SystemTraceFn gSystemTrace = 0;

typedef void (*XNotifyQueueUIFn)(DWORD, DWORD, ULONGLONG, WCHAR*, void*);
typedef DWORD (*UserThreadProcFn)(void*);
typedef HANDLE (*XamCreateThreadFn)(void*, DWORD, UserThreadProcFn, void*, DWORD, DWORD*);

static void TraceSystem(const char* text) {
    if (!text) return;
    DbgPrint("[iPhoneTether360:NOTIFY] %s\n", text);
    if (gSystemTrace) gSystemTrace(text);
}

static bool ConvertAscii(const char* ascii_text, WCHAR* out, size_t capacity) {
    if (!ascii_text || !ascii_text[0] || !out || capacity < 2u) return false;
    size_t i = 0;
    while (ascii_text[i] && i + 1u < capacity) {
        const unsigned char c = static_cast<unsigned char>(ascii_text[i]);
        out[i] = c < 0x80u ? static_cast<WCHAR>(c) : static_cast<WCHAR>('?');
        ++i;
    }
    out[i] = 0;
    return i != 0;
}

static XNotifyQueueUIFn ResolveNotify() {
    void* proc = 0;
    it360_platform::ResolveStatus diagnostic;
    if (!it360_platform::ResolveModuleOrdinal("xam.xex", 656u, &proc, &diagnostic) || !proc) {
        if (diagnostic.has_status) {
            DbgPrint("[iPhoneTether360:NOTIFY] XNotify resolve failed | ordinal=656 operation=%s NTSTATUS=0x%08x\n",
                     it360_platform::ResolveOperationName(diagnostic.operation),
                     static_cast<unsigned>(diagnostic.status));
        } else {
            DbgPrint("[iPhoneTether360:NOTIFY] XNotify resolve failed | ordinal=656 operation=%s status=unavailable\n",
                     it360_platform::ResolveOperationName(diagnostic.operation));
        }
        return 0;
    }
    return reinterpret_cast<XNotifyQueueUIFn>(proc);
}

static bool DirectShowWide(WCHAR* text) {
    XNotifyQueueUIFn notify = ResolveNotify();
    if (!notify || !text || !text[0]) return false;
    notify(14u, 0u, 2u, text, 0);
    return true;
}

static DWORD SystemNotifyThread(void*) {
    TraceSystem("NOTIFY03 | XAM notification thread started");
    TraceSystem("NOTIFY04 | resolving XNotifyQueueUI");
    XNotifyQueueUIFn notify = ResolveNotify();
    if (!notify) {
        TraceSystem("NOTIFY04A | XNotifyQueueUI resolve failed");
        it360_platform::AtomicExchange(&gSystemThreadBusy, 0);
        return 0;
    }

    TraceSystem("NOTIFY05 | XNotifyQueueUI resolved");
    TraceSystem("NOTIFY06 | calling XNotifyQueueUI");
    notify(14u, 0u, 2u, gSystemThreadText, 0);
    TraceSystem("NOTIFY07 | XNotifyQueueUI returned");
    it360_platform::AtomicExchange(&gSystemThreadBusy, 0);
    return 0;
}

static bool DispatchSystem(WCHAR* text) {
    if (!text || !text[0]) return false;

    TraceSystem("NOTIFY00 | system notification requested");
    if (it360_platform::AtomicCompareExchange(&gSystemThreadBusy, 1, 0) != 0) {
        TraceSystem("NOTIFY00A | system notification dispatcher busy");
        return false;
    }

    size_t i = 0;
    while (text[i] && i + 1u < (sizeof(gSystemThreadText) / sizeof(gSystemThreadText[0]))) {
        gSystemThreadText[i] = text[i];
        ++i;
    }
    gSystemThreadText[i] = 0;

    TraceSystem("NOTIFY01 | resolving XAM CreateThread");
    void* proc = 0;
    it360_platform::ResolveStatus diagnostic;
    if (!it360_platform::ResolveModuleOrdinal("xam.xex", 1084u, &proc, &diagnostic) || !proc) {
        if (diagnostic.has_status) {
            DbgPrint("[iPhoneTether360:NOTIFY] CreateThread resolve failed | ordinal=1084 operation=%s NTSTATUS=0x%08x\n",
                     it360_platform::ResolveOperationName(diagnostic.operation),
                     static_cast<unsigned>(diagnostic.status));
        } else {
            DbgPrint("[iPhoneTether360:NOTIFY] CreateThread resolve failed | ordinal=1084 operation=%s status=unavailable\n",
                     it360_platform::ResolveOperationName(diagnostic.operation));
        }
        TraceSystem("NOTIFY01A | XAM CreateThread resolve failed");
        it360_platform::AtomicExchange(&gSystemThreadBusy, 0);
        return false;
    }

    TraceSystem("NOTIFY02 | XAM CreateThread resolved");
    XamCreateThreadFn create_thread = reinterpret_cast<XamCreateThreadFn>(proc);
    DWORD thread_id = 0;
    HANDLE thread = create_thread(0, 0u, &SystemNotifyThread, 0, 0u, &thread_id);
    if (!thread) {
        TraceSystem("NOTIFY02A | XAM CreateThread returned NULL");
        it360_platform::AtomicExchange(&gSystemThreadBusy, 0);
        return false;
    }

    const NTSTATUS close_status = NtClose(thread);
    if (it360_platform::FailedStatus(close_status)) {
        DbgPrint("[iPhoneTether360:NOTIFY] NtClose(system notification thread) failed NTSTATUS=0x%08x\n",
                 static_cast<unsigned>(close_status));
    }
    return true;
}

#endif

} // namespace

void SetSystemTrace(SystemTraceFn trace) {
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    gSystemTrace = trace;
#else
    (void)trace;
#endif
}

bool ShowTitle(const char* ascii_text) {
    if (!ascii_text || !ascii_text[0]) return false;
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    WCHAR text[160];
    if (!ConvertAscii(ascii_text, text, sizeof(text) / sizeof(text[0]))) return false;
    return DirectShowWide(text);
#else
    (void)ascii_text;
    return true;
#endif
}

bool ShowSystem(const char* ascii_text) {
    if (!ascii_text || !ascii_text[0]) return false;
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    WCHAR text[160];
    if (!ConvertAscii(ascii_text, text, sizeof(text) / sizeof(text[0]))) return false;
    return DispatchSystem(text);
#else
    (void)ascii_text;
    return true;
#endif
}

bool Starting() { return ShowSystem("Starting iPhoneTether360..."); }
bool Ready() { return ShowSystem("iPhoneTether360 Ready"); }
bool AlreadyRunning() { return ShowSystem("iPhoneTether360 Already Running"); }
bool LicenceFound() { return ShowSystem("License Has Been Found"); }
bool NoLicence() { return ShowSystem("No License found for this Xbox"); }
bool InternetNotFound() { return ShowSystem("iPhone Data Not Working"); }

} // namespace it360_notify
