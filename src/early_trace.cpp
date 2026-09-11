#include "platform/xbox_platform.h"
#include "shared/app_path.h"
#include "shared/version.h"
#include "early_trace.h"

namespace it360_early_trace {
namespace {

// Emergency persistence exists specifically for the interval before the Core
// can safely resolve its application directory. This path was used by the
// earlier hardware diagnostic build and requires no CRT path helpers.
static const char kEmergencyNativePath[] =
    "\\Device\\Harddisk0\\Partition1\\iPhoneTether360.log";
static const char kEmergencyVisiblePath[] =
    "Hdd1:\\iPhoneTether360.log";

static char gAppDirectory[512];
static char gBootLogPath[560];
static char gRuntimeLogPath[560];

// Keep a bounded copy of all pre-handoff checkpoints. If Core survives long
// enough to resolve its application directory, these lines are replayed into
// the normal app-local Boot.log. If it does not, the emergency HDD file still
// contains the flushed checkpoints needed to locate the crash boundary.
static const size_t kReplayCapacity = 8192u;
static char gReplay[kReplayCapacity];
static size_t gReplayLength = 0;
static bool gReplayOverflow = false;

static volatile LONG gFailures = 0;
static volatile LONG gPersistentReady = 0;
static volatile LONG gNormalReady = 0;
static volatile DWORD gLastStatus = 0;

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
static volatile LONG gLock = 0;
static HANDLE gEmergencyFile = 0;
static HANDLE gNormalFile = 0;
#endif

static size_t RawLength(const char* text) {
    if (!text) return 0;
    size_t n = 0;
    while (text[n]) ++n;
    return n;
}

static void RecordFailure(DWORD status) {
    gLastStatus = status;
#if defined(__GNUC__) || defined(__clang__)
    __sync_fetch_and_add(&gFailures, 1);
#else
    ++gFailures;
#endif
}

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
static bool Acquire() {
#if defined(__GNUC__) || defined(__clang__)
    for (unsigned i = 0; i < 100000u; ++i) {
        if (__sync_val_compare_and_swap(&gLock, 0, 1) == 0) return true;
    }
#else
    for (unsigned i = 0; i < 100000u; ++i) {
        if (gLock == 0) {
            gLock = 1;
            return true;
        }
    }
#endif
    RecordFailure(0xE0000001u);
    return false;
}

static void Release() {
#if defined(__GNUC__) || defined(__clang__)
    __sync_lock_release(&gLock);
#else
    gLock = 0;
#endif
}

#endif

static void CaptureReplay(const char* data, size_t length, bool append_crlf) {
    if (!data || !length || gReplayOverflow) return;

    const size_t extra = append_crlf ? 2u : 0u;
    if (length + extra > kReplayCapacity - gReplayLength) {
        gReplayOverflow = true;
        return;
    }

    for (size_t i = 0; i < length; ++i)
        gReplay[gReplayLength++] = data[i];
    if (append_crlf) {
        gReplay[gReplayLength++] = '\r';
        gReplay[gReplayLength++] = '\n';
    }
}

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)

static bool NativeStatusOk(NTSTATUS status) {
    return static_cast<int32_t>(status) >= 0;
}

static HANDLE NativeOpenAppend(const char* path, DWORD* status_out) {
    if (status_out) *status_out = 0;
    if (!path || !path[0]) {
        if (status_out) *status_out = 0xE0000002u;
        return 0;
    }

    const size_t length = RawLength(path);
    if (!length || length > 0xFFFEu) {
        if (status_out) *status_out = 0xE0000003u;
        return 0;
    }

    ANSI_STRING name;
    name.Length = static_cast<uint16_t>(length);
    name.MaximumLength = static_cast<uint16_t>(length + 1u);
    name.Buffer = const_cast<char*>(path);

    OBJECT_ATTRIBUTES attributes;
    attributes.root_directory = 0;
    attributes.name_ptr = &name;
    attributes.attributes = OBJ_CASE_INSENSITIVE;

    IO_STATUS_BLOCK io;
    io.Status = 0;
    io.Information = 0;

    HANDLE file = 0;
    const NTSTATUS status = NtCreateFile(
        &file,
        FILE_APPEND_DATA | SYNCHRONIZE,
        &attributes,
        &io,
        0,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN_IF,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE
    );

    if (!NativeStatusOk(status) || !NativeStatusOk(io.Status) || !file) {
        if (status_out) {
            if (!NativeStatusOk(status)) *status_out = static_cast<DWORD>(status);
            else if (!NativeStatusOk(io.Status)) *status_out = static_cast<DWORD>(io.Status);
            else *status_out = 0xE0000004u;
        }
        return 0;
    }

    return file;
}

static bool NativeWriteAll(HANDLE file, const char* data, size_t length, DWORD* status_out) {
    if (status_out) *status_out = 0;
    if (!file || (!data && length)) {
        if (status_out) *status_out = 0xE0000005u;
        return false;
    }

    size_t written = 0;
    while (written < length) {
        IO_STATUS_BLOCK io;
        io.Status = 0;
        io.Information = 0;

        const size_t remain = length - written;
        const DWORD chunk = remain > 0x7FFFFFFFu
            ? 0x7FFFFFFFu
            : static_cast<DWORD>(remain);

        const NTSTATUS status = NtWriteFile(
            file,
            0,
            0,
            0,
            &io,
            const_cast<char*>(data + written),
            chunk,
            0
        );

        if (!NativeStatusOk(status) || !NativeStatusOk(io.Status) ||
            io.Information == 0 || io.Information > chunk) {
            if (status_out) {
                if (!NativeStatusOk(status)) *status_out = static_cast<DWORD>(status);
                else if (!NativeStatusOk(io.Status)) *status_out = static_cast<DWORD>(io.Status);
                else *status_out = 0xE0000006u;
            }
            return false;
        }

        written += io.Information;
    }

    return true;
}

static bool NativeFlush(HANDLE file, DWORD* status_out) {
    if (status_out) *status_out = 0;
    if (!file) {
        if (status_out) *status_out = 0xE0000007u;
        return false;
    }

    IO_STATUS_BLOCK io;
    io.Status = 0;
    io.Information = 0;
    const NTSTATUS status = NtFlushBuffersFile(file, &io);
    if (!NativeStatusOk(status) || !NativeStatusOk(io.Status)) {
        if (status_out)
            *status_out = !NativeStatusOk(status)
                ? static_cast<DWORD>(status)
                : static_cast<DWORD>(io.Status);
        return false;
    }
    return true;
}

static void NativeClose(HANDLE* file) {
    if (!file || !*file) return;
    const NTSTATUS status = NtClose(*file);
    *file = 0;
    if (!NativeStatusOk(status)) {
        RecordFailure(static_cast<DWORD>(status));
        DbgPrint("[iPhoneTether360:EARLY] NtClose failed NTSTATUS=0x%08x\n",
                 static_cast<unsigned>(status));
    }
}

static bool EnsureEmergencyOpenUnlocked() {
    if (gEmergencyFile) return true;
    DWORD status = 0;
    gEmergencyFile = NativeOpenAppend(kEmergencyNativePath, &status);
    if (!gEmergencyFile) {
        RecordFailure(status ? status : 0xE0000008u);
        return false;
    }
    gLastStatus = 0;
    it360_platform::AtomicExchange(&gPersistentReady, 1);
    return true;
}

static bool EnsureNormalOpenUnlocked() {
    if (gNormalFile) return true;
    if (!gBootLogPath[0]) return false;

    DWORD status = 0;
    gNormalFile = NativeOpenAppend(gBootLogPath, &status);
    if (!gNormalFile) {
        RecordFailure(status ? status : 0xE0000009u);
        return false;
    }
    return true;
}

static bool PersistHandleUnlocked(
    HANDLE file,
    const char* data,
    size_t length,
    bool append_crlf
) {
    DWORD status = 0;
    bool ok = NativeWriteAll(file, data, length, &status);
    if (ok && append_crlf)
        ok = NativeWriteAll(file, "\r\n", 2u, &status);
    if (ok)
        ok = NativeFlush(file, &status);

    if (!ok) RecordFailure(status ? status : 0xE000000Au);
    return ok;
}

static bool PersistEmergencyUnlocked(const char* data, size_t length, bool append_crlf) {
    if (!EnsureEmergencyOpenUnlocked()) return false;
    if (PersistHandleUnlocked(gEmergencyFile, data, length, append_crlf)) return true;
    NativeClose(&gEmergencyFile);
    if (it360_platform::AtomicCompareExchange(&gNormalReady, 0, 0) == 0)
        it360_platform::AtomicExchange(&gPersistentReady, 0);
    return false;
}

static bool PersistNormalUnlocked(const char* data, size_t length, bool append_crlf) {
    if (!EnsureNormalOpenUnlocked()) return false;
    if (PersistHandleUnlocked(gNormalFile, data, length, append_crlf)) return true;
    NativeClose(&gNormalFile);
    it360_platform::AtomicExchange(&gNormalReady, 0);
    if (!gEmergencyFile)
        it360_platform::AtomicExchange(&gPersistentReady, 0);
    return false;
}

static bool ReplayEmergencyIntoNormalUnlocked() {
    static const char begin[] =
        "\r\n--- PRE-CRT EMERGENCY TRACE REPLAY BEGIN ---\r\n";
    static const char end[] =
        "--- PRE-CRT EMERGENCY TRACE REPLAY END ---\r\n";
    static const char overflow[] =
        "EARLY TRACE WARNING: replay buffer overflowed; emergency HDD log contains the authoritative pre-CRT tail\r\n";

    if (!PersistHandleUnlocked(gNormalFile, begin, sizeof(begin) - 1u, false)) return false;
    if (gReplayLength && !PersistHandleUnlocked(gNormalFile, gReplay, gReplayLength, false)) return false;
    if (gReplayOverflow && !PersistHandleUnlocked(gNormalFile, overflow, sizeof(overflow) - 1u, false)) return false;
    return PersistHandleUnlocked(gNormalFile, end, sizeof(end) - 1u, false);
}

#endif

static bool AppendLeaf(const char* directory, const char* leaf, char* out, size_t capacity) {
    return it360_app_path::Join(directory, leaf, out, capacity);
}

static bool Persist(const char* data, size_t length, bool append_crlf) {
    if (!data) return false;

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    if (!Acquire()) return false;

    const bool normal = it360_platform::AtomicCompareExchange(&gNormalReady, 0, 0) != 0;
    if (!normal) CaptureReplay(data, length, append_crlf);

    const bool ok = normal
        ? PersistNormalUnlocked(data, length, append_crlf)
        : PersistEmergencyUnlocked(data, length, append_crlf);

    Release();
    return ok;
#else
    CaptureReplay(data, length, append_crlf);
    (void)length;
    (void)append_crlf;
    return true;
#endif
}

} // namespace

bool ConfigureApplicationPaths() {
    WriteLiteral("EARLY PATH 00: resolving Core application directory");

    gAppDirectory[0] = 0;
    gBootLogPath[0] = 0;
    gRuntimeLogPath[0] = 0;

    it360_platform::ResolveStatus path_diagnostic;
    if (!it360_app_path::ResolveAppDirectory(gAppDirectory, sizeof(gAppDirectory), &path_diagnostic)) {
        RecordFailure(path_diagnostic.has_status
            ? static_cast<DWORD>(path_diagnostic.status)
            : 0xE0000010u);
        WriteLiteral("EARLY PATH 01 FAILED: Core application directory could not be resolved; emergency HDD trace remains active");
        WriteLiteral(it360_platform::ResolveOperationName(path_diagnostic.operation));
        if (path_diagnostic.has_status)
            WriteHex32("EARLY PATH 01 resolver NTSTATUS=", static_cast<uint32_t>(path_diagnostic.status));
        else
            WriteLiteral("EARLY PATH 01 resolver status=unavailable");
        return false;
    }

    WriteLiteral("EARLY PATH 01: Core application directory resolved");

    if (!AppendLeaf(gAppDirectory, "Boot.log", gBootLogPath, sizeof(gBootLogPath)) ||
        !AppendLeaf(gAppDirectory, "Log.txt", gRuntimeLogPath, sizeof(gRuntimeLogPath))) {
        RecordFailure(0xE0000011u);
        WriteLiteral("EARLY PATH 02 FAILED: app-local Boot.log/Log.txt path construction failed; emergency HDD trace remains active");
        gBootLogPath[0] = 0;
        gRuntimeLogPath[0] = 0;
        return false;
    }

    WriteLiteral("EARLY PATH 02: app-local Boot.log and Log.txt paths constructed");

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    if (!Acquire()) return false;

    if (!EnsureNormalOpenUnlocked()) {
        Release();
        WriteLiteral("EARLY PATH 03 FAILED: app-local Boot.log could not be opened; emergency HDD trace remains active");
        return false;
    }

    // Commit the normal file before changing routing. The complete pre-CRT
    // trace is replayed and flushed first, so a successful handoff produces a
    // self-contained app-local Boot.log.
    if (!ReplayEmergencyIntoNormalUnlocked()) {
        NativeClose(&gNormalFile);
        Release();
        WriteLiteral("EARLY PATH 03 FAILED: pre-CRT replay into app-local Boot.log failed; emergency HDD trace remains active");
        return false;
    }

    static const char normalReady[] =
        "EARLY PATH 03: app-local Boot.log opened; pre-CRT trace replayed and flushed";
    if (!PersistHandleUnlocked(gNormalFile, normalReady, sizeof(normalReady) - 1u, true)) {
        NativeClose(&gNormalFile);
        Release();
        WriteLiteral("EARLY PATH 03 FAILED: app-local Boot.log final handoff flush failed; emergency HDD trace remains active");
        return false;
    }

    if (gEmergencyFile) {
        static const char handoff[] =
            "EARLY HANDOFF: switching future checkpoints to the app-local Boot.log";
        (void)PersistHandleUnlocked(gEmergencyFile, handoff, sizeof(handoff) - 1u, true);
        NativeClose(&gEmergencyFile);
    }

    it360_platform::AtomicExchange(&gNormalReady, 1);
    it360_platform::AtomicExchange(&gPersistentReady, 1);
    gLastStatus = 0;
    Release();
#else
    it360_platform::AtomicExchange(&gNormalReady, 1);
    it360_platform::AtomicExchange(&gPersistentReady, 1);
#endif

    DbgPrint("[iPhoneTether360:EARLY] normal app-local Boot.log handoff complete\n");
    return true;
}

bool BeginSessionLiteral(const char* text) {
    static const char separator[] =
        "\r\n"
        "============================================================\r\n"
        IT360_VERSION_STRING " Core diagnostic session\r\n"
        "============================================================\r\n";

    // Do not suppress the actual checkpoint if the emergency file itself is
    // unavailable. Persist() still captures the separator in RAM, and
    // WriteLiteral() records the first instruction-boundary message for later
    // replay while also sending it to DbgPrint.
    const bool separator_ok = Persist(separator, sizeof(separator) - 1u, false);
    const bool line_ok = WriteLiteral(text);
    return separator_ok && line_ok;
}

bool WriteLiteral(const char* text) {
    if (!text) return false;

    DbgPrint("[iPhoneTether360:EARLY] %s\n", text);
    const bool ok = Persist(text, RawLength(text), true);
    if (!ok) {
        DbgPrint(
            "[iPhoneTether360:EARLY] persistence FAILED status=%08x failures=%u\n",
            static_cast<unsigned>(gLastStatus),
            static_cast<unsigned>(FailureCount())
        );
    }
    return ok;
}

bool WriteBuffer(const char* data, size_t length, bool append_crlf) {
    if (!data) return false;
    return Persist(data, length, append_crlf);
}

bool WriteHex32(const char* prefix, uint32_t value) {
    char line[160];
    size_t n = 0;
    if (prefix) {
        while (prefix[n] && n + 1u < sizeof(line)) {
            line[n] = prefix[n];
            ++n;
        }
    }
    if (n + 10u >= sizeof(line)) return false;

    line[n++] = '0';
    line[n++] = 'x';
    static const char digits[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4)
        line[n++] = digits[(value >> shift) & 0x0Fu];
    line[n] = 0;
    return WriteLiteral(line);
}

bool PersistentReady() {
    return it360_platform::AtomicCompareExchange(&gPersistentReady, 0, 0) != 0;
}

bool NormalBootLogReady() {
    return it360_platform::AtomicCompareExchange(&gNormalReady, 0, 0) != 0;
}

uint32_t FailureCount() {
    return static_cast<uint32_t>(
        it360_platform::AtomicCompareExchange(&gFailures, 0, 0)
    );
}

uint32_t LastStatus() {
    return static_cast<uint32_t>(gLastStatus);
}

const char* NativePath() {
    return NormalBootLogReady() ? gBootLogPath : kEmergencyNativePath;
}

const char* VisiblePath() {
    return NormalBootLogReady() ? gBootLogPath : kEmergencyVisiblePath;
}

const char* EmergencyVisiblePath() {
    return kEmergencyVisiblePath;
}

const char* RuntimeLogPath() {
    return gRuntimeLogPath;
}

} // namespace it360_early_trace
