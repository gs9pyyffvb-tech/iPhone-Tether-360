#include "shared/notify.h"
#include "platform/xbox_platform.h"

namespace it360_notify {
namespace {

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)

static const uint64_t kDirectNotifyGapMs = 2500ULL;
static const DWORD kUserProcessType = 1u;
static uint64_t gLastShowMs = 0;
static bool gHasShown = false;
static volatile LONG gUserThreadBusy = 0;
static WCHAR gUserThreadText[160];

typedef void (*XNotifyQueueUIFn)(DWORD, DWORD, ULONGLONG, WCHAR*, void*);
typedef DWORD (*UserThreadProcFn)(void*);
typedef HANDLE (*XamCreateThreadFn)(void*, DWORD, UserThreadProcFn, void*, DWORD, DWORD*);

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
    notify(14u, 0xFFu, 1u, text, 0);
    return true;
}

static DWORD UserNotifyThread(void*) {
    (void)DirectShowWide(gUserThreadText);
    it360_platform::AtomicExchange(&gUserThreadBusy, 0);
    return 0;
}

static bool DispatchFromUserThread(WCHAR* text) {
    if (!text || !text[0]) return false;

    // XNotifyQueueUI is a XAM UI call. Established Xbox 360 homebrew practice
    // is to marshal it through XAM CreateThread when the caller is a system
    // thread. Loader/LicenseID normally arrive here as user-process threads and
    // can execute the UI call directly.
    if (static_cast<DWORD>(KeGetCurrentProcessType()) == kUserProcessType)
        return DirectShowWide(text);

    if (it360_platform::AtomicCompareExchange(&gUserThreadBusy, 1, 0) != 0) {
        DbgPrint("[iPhoneTether360:NOTIFY] user-thread marshal busy; notification deferred/dropped\n");
        return false;
    }

    size_t i = 0;
    while (text[i] && i + 1u < (sizeof(gUserThreadText) / sizeof(gUserThreadText[0]))) {
        gUserThreadText[i] = text[i];
        ++i;
    }
    gUserThreadText[i] = 0;

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
        it360_platform::AtomicExchange(&gUserThreadBusy, 0);
        return false;
    }

    XamCreateThreadFn create_thread = reinterpret_cast<XamCreateThreadFn>(proc);
    DWORD thread_id = 0;
    HANDLE thread = create_thread(0, 0u, &UserNotifyThread, 0, 0u, &thread_id);
    if (!thread) {
        DbgPrint("[iPhoneTether360:NOTIFY] XAM CreateThread returned NULL | ordinal=1084 status=unavailable\n");
        it360_platform::AtomicExchange(&gUserThreadBusy, 0);
        return false;
    }

    const NTSTATUS close_status = NtClose(thread);
    if (it360_platform::FailedStatus(close_status)) {
        DbgPrint("[iPhoneTether360:NOTIFY] NtClose(user notification thread) failed NTSTATUS=0x%08x\n",
                 static_cast<unsigned>(close_status));
    }
    return true;
}

static void WaitForReadableGap() {
    const uint64_t now = it360_platform::MonotonicMs();
    if (!now) {
        if (gHasShown) it360_platform::SleepMs(static_cast<DWORD>(kDirectNotifyGapMs));
        return;
    }
    if (!gLastShowMs || now >= gLastShowMs + kDirectNotifyGapMs) return;
    const uint64_t remaining = (gLastShowMs + kDirectNotifyGapMs) - now;
    if (remaining > 0 && remaining <= kDirectNotifyGapMs)
        it360_platform::SleepMs(static_cast<DWORD>(remaining));
}

#endif

} // namespace

bool Show(const char* ascii_text) {
    if (!ascii_text || !ascii_text[0]) return false;

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    WCHAR text[160];
    size_t i = 0;
    while (ascii_text[i] && i + 1u < (sizeof(text) / sizeof(text[0]))) {
        const unsigned char c = static_cast<unsigned char>(ascii_text[i]);
        text[i] = c < 0x80u ? static_cast<WCHAR>(c) : static_cast<WCHAR>('?');
        ++i;
    }
    text[i] = 0;

    WaitForReadableGap();
    const bool shown = DispatchFromUserThread(text);
    if (shown) {
        gLastShowMs = it360_platform::MonotonicMs();
        gHasShown = true;
    }
    return shown;
#else
    (void)ascii_text;
    return true;
#endif
}

bool Starting() { return Show("Starting iPhoneTether360..."); }
bool Ready() { return Show("iPhoneTether360 Ready"); }
bool AlreadyRunning() { return Show("iPhoneTether360 Already Running"); }
bool LicenceFound() { return Show("License Has Been Found"); }
bool NoLicence() { return Show("No License found for this Xbox"); }
bool InternetNotFound() { return Show("iPhone Data Not Working"); }

} // namespace it360_notify
