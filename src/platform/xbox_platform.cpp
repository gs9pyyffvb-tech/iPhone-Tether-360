#include "xbox_platform.h"
#include <stdio.h>

namespace it360_platform {

bool FailedStatus(NTSTATUS status) {
    return static_cast<int32_t>(status) < 0;
}

const char* ResolveOperationName(ResolveOperation operation) {
    switch (operation) {
    case ResolveOperationInvalidArgument: return "argument validation";
    case ResolveOperationXexGetModuleHandle: return "XexGetModuleHandle";
    case ResolveOperationNullModuleHandle: return "XexGetModuleHandle(null module)";
    case ResolveOperationXexGetProcedureAddress: return "XexGetProcedureAddress";
    case ResolveOperationNullProcedureAddress: return "XexGetProcedureAddress(null procedure)";
    default: return "none";
    }
}

const char* ThreadStartOperationName(ThreadStartOperation operation) {
    switch (operation) {
    case ThreadStartOperationInvalidEntry: return "thread entry validation";
    case ThreadStartOperationExAllocatePoolWithTag: return "ExAllocatePoolWithTag";
    case ThreadStartOperationExCreateThread: return "ExCreateThread";
    case ThreadStartOperationNullThreadHandle: return "ExCreateThread(null handle)";
    default: return "none";
    }
}

LONG AtomicCompareExchange(volatile LONG* value, LONG exchange, LONG comparand) {
#if defined(__GNUC__) || defined(__clang__)
    return __sync_val_compare_and_swap(value, comparand, exchange);
#else
    LONG old = *value;
    if (old == comparand) *value = exchange;
    return old;
#endif
}

LONG AtomicExchange(volatile LONG* value, LONG exchange) {
#if defined(__GNUC__) || defined(__clang__)
    return __sync_lock_test_and_set(value, exchange);
#else
    LONG old = *value;
    *value = exchange;
    return old;
#endif
}

LONG AtomicIncrement(volatile LONG* value) {
#if defined(__GNUC__) || defined(__clang__)
    return __sync_add_and_fetch(value, 1);
#else
    return ++(*value);
#endif
}

void SleepMs(DWORD milliseconds) {
#if defined(IT360_OPENXECHAIN)
    // Xbox kernel delays are expressed in signed 100-ns units. A negative
    // value is a relative delay.
    int64_t interval = -static_cast<int64_t>(milliseconds) * 10000LL;
    KeDelayExecutionThread(0, 0, &interval);
#else
    (void)milliseconds;
#endif
}

uint64_t MonotonicMs() {
#if defined(IT360_OPENXECHAIN)
    int64_t now = 0;
    KeQuerySystemTime(&now);
    return now > 0 ? static_cast<uint64_t>(now) / 10000ULL : 0ULL;
#else
    return static_cast<uint64_t>(0);
#endif
}

void FlushInstructionCache(void* address, size_t length) {
#if defined(IT360_OPENXECHAIN)
    if (!address || !length) return;
    // Xenon has 128-byte I/D cache lines. Kernel code patches must write back
    // the modified data cache lines and invalidate the corresponding I-cache
    // lines before execution can safely continue on PowerPC.
    const uintptr_t line = 128u;
    uintptr_t start = reinterpret_cast<uintptr_t>(address) & ~(line - 1u);
    uintptr_t end = (reinterpret_cast<uintptr_t>(address) + length + line - 1u) & ~(line - 1u);
    for (uintptr_t p = start; p < end; p += line)
        __asm__ volatile("dcbst 0,%0" : : "r"(p) : "memory");
    __asm__ volatile("sync" : : : "memory");
    for (uintptr_t p = start; p < end; p += line)
        __asm__ volatile("icbi 0,%0" : : "r"(p) : "memory");
    __asm__ volatile("sync\n\tisync" : : : "memory");
#else
    (void)address;
    (void)length;
#endif
}

#if defined(IT360_OPENXECHAIN)
struct ThreadStartBlock {
    ThreadFn entry;
    void* context;
};

static uint32_t RawThreadThunk(void* opaque) {
    ThreadStartBlock* block = static_cast<ThreadStartBlock*>(opaque);
    ThreadFn entry = block ? block->entry : 0;
    void* context = block ? block->context : 0;
    if (block) ExFreePool(block);
    uint32_t result = entry ? entry(context) : 0;
    // We deliberately create a raw kernel thread because OpenXeChain does not
    // expose the legacy runtime thread-start helper. Never return through an
    // unknown LR: terminate the raw thread explicitly after the project worker
    // returns.
    ExTerminateThread(result);
    return result; // unreachable on hardware; keeps the C++ signature complete.
}
#endif

bool StartDetachedThread(ThreadFn entry, void* context, ThreadStartStatus* diagnostic) {
    if (diagnostic) *diagnostic = ThreadStartStatus();
    if (!entry) {
        if (diagnostic) diagnostic->operation = ThreadStartOperationInvalidEntry;
        return false;
    }
#if defined(IT360_OPENXECHAIN)
    ThreadStartBlock* block = static_cast<ThreadStartBlock*>(
        ExAllocatePoolWithTag(sizeof(ThreadStartBlock), 0x49543336u)); // 'IT36'
    if (!block) {
        if (diagnostic) diagnostic->operation = ThreadStartOperationExAllocatePoolWithTag;
        return false;
    }
    block->entry = entry;
    block->context = context;

    HANDLE thread = 0;
    uint32_t thread_id = 0;
    // 0x2 is EX_CREATE_FLAG_SYSTEM. With apiThreadStartup == NULL this is a
    // raw thread; RawThreadThunk provides deterministic termination semantics.
    NTSTATUS status = ExCreateThread(&thread, 0, &thread_id, 0,
                                     reinterpret_cast<void*>(RawThreadThunk), block, 0x2u);
    if (diagnostic) {
        diagnostic->operation = ThreadStartOperationExCreateThread;
        diagnostic->status = status;
        diagnostic->has_status = true;
    }
    if (FailedStatus(status)) {
        ExFreePool(block);
        return false;
    }
    if (!thread) {
        ExFreePool(block);
        if (diagnostic) {
            diagnostic->operation = ThreadStartOperationNullThreadHandle;
            diagnostic->has_status = false;
        }
        return false;
    }

    // The kernel owns the running thread; close only our handle reference.
    const NTSTATUS close_status = NtClose(thread);
    if (diagnostic) {
        diagnostic->close_status = close_status;
        diagnostic->close_status_valid = true;
        diagnostic->operation = ThreadStartOperationNone;
        diagnostic->status = 0;
        diagnostic->has_status = false;
    }
    return true;
#else
    (void)context;
    return true;
#endif
}

bool ResolveModuleOrdinal(const char* module_name, DWORD ordinal, void** out,
                          ResolveStatus* diagnostic) {
    if (diagnostic) *diagnostic = ResolveStatus();
    if (!module_name || !out) {
        if (diagnostic) diagnostic->operation = ResolveOperationInvalidArgument;
        return false;
    }
    *out = 0;
#if defined(IT360_OPENXECHAIN)
    HMODULE module = 0;
    NTSTATUS status = XexGetModuleHandle(module_name, &module);
    if (diagnostic) {
        diagnostic->operation = ResolveOperationXexGetModuleHandle;
        diagnostic->status = status;
        diagnostic->has_status = true;
    }
    if (FailedStatus(status)) return false;
    if (!module) {
        if (diagnostic) {
            diagnostic->operation = ResolveOperationNullModuleHandle;
            diagnostic->has_status = false;
        }
        return false;
    }

    status = XexGetProcedureAddress(module, ordinal, out);
    if (diagnostic) {
        diagnostic->operation = ResolveOperationXexGetProcedureAddress;
        diagnostic->status = status;
        diagnostic->has_status = true;
    }
    if (FailedStatus(status)) return false;
    if (!*out) {
        if (diagnostic) {
            diagnostic->operation = ResolveOperationNullProcedureAddress;
            diagnostic->has_status = false;
        }
        return false;
    }

    if (diagnostic) *diagnostic = ResolveStatus();
    return true;
#else
    (void)ordinal;
    if (diagnostic) diagnostic->operation = ResolveOperationXexGetModuleHandle;
    return false;
#endif
}

WORD KernelBuild(ResolveStatus* diagnostic) {
#if defined(IT360_OPENXECHAIN)
    struct KernelVersion {
        WORD major;
        WORD minor;
        WORD build;
        WORD qfe;
    };
    void* address = 0;
    // xboxkrnl export 344 is the XboxKrnlVersion data export. Resolving it at
    // runtime avoids depending on a toolchain-specific variable declaration.
    if (!ResolveModuleOrdinal("xboxkrnl.exe", 344, &address, diagnostic) || !address) return 0;
    const KernelVersion* version = static_cast<const KernelVersion*>(address);
    return version->build;
#else
    (void)diagnostic;
    return 17559;
#endif
}

FileHandle OpenAppend(const char* path) {
    if (!path) return 0;
#if defined(IT360_OPENXECHAIN)
    HANDLE file = CreateFileA(const_cast<char*>(path), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        DbgPrint("[iPhoneTether360:PLATFORM] file open failed | operation=CreateFileA(append) result=INVALID_HANDLE_VALUE status=unavailable\n");
        return 0;
    }
    SetFilePointer(file, 0, 0, 2u);
    return file;
#else
    return static_cast<FileHandle>(fopen(path, "ab"));
#endif
}

FileHandle OpenRead(const char* path) {
    if (!path) return 0;
#if defined(IT360_OPENXECHAIN)
    HANDLE file = CreateFileA(const_cast<char*>(path), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    return file == INVALID_HANDLE_VALUE ? 0 : file;
#else
    return static_cast<FileHandle>(fopen(path, "rb"));
#endif
}

FileHandle OpenTruncate(const char* path) {
    if (!path) return 0;
#if defined(IT360_OPENXECHAIN)
    HANDLE file = CreateFileA(const_cast<char*>(path), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        DbgPrint("[iPhoneTether360:PLATFORM] file open failed | operation=CreateFileA(truncate) result=INVALID_HANDLE_VALUE status=unavailable\n");
        return 0;
    }
    return file;
#else
    return static_cast<FileHandle>(fopen(path, "wb"));
#endif
}

bool ReadBounded(FileHandle file, void* data, size_t capacity, size_t* bytes_read) {
    if (!file || !data || !capacity || !bytes_read || capacity > 0xFFFFFFFFu) return false;
    *bytes_read = 0;
#if defined(IT360_OPENXECHAIN)
    uint32_t got = 0;
    if (!ReadFile(file, data, static_cast<uint32_t>(capacity), &got, 0)) {
        DbgPrint("[iPhoneTether360:PLATFORM] file read failed | operation=ReadFile result=FALSE status=unavailable requested=0x%08x\n",
                 static_cast<unsigned>(capacity));
        return false;
    }
    *bytes_read = got;
    // If the caller filled the entire bounded buffer, prove EOF with one
    // additional byte. This rejects oversized/corrupt pair records rather
    // than silently accepting a truncated record.
    if (got == capacity) {
        BYTE extra = 0;
        uint32_t extra_got = 0;
        if (!ReadFile(file, &extra, 1, &extra_got, 0)) {
            DbgPrint("[iPhoneTether360:PLATFORM] file EOF probe failed | operation=ReadFile result=FALSE status=unavailable\n");
            return false;
        }
        if (extra_got != 0) return false;
    }
    return true;
#else
    FILE* f = static_cast<FILE*>(file);
    *bytes_read = fread(data, 1, capacity, f);
    return ferror(f) == 0;
#endif
}

bool WriteAll(FileHandle file, const void* data, size_t bytes) {
    if (!file || (!data && bytes) || bytes > 0xFFFFFFFFu) return false;
#if defined(IT360_OPENXECHAIN)
    const BYTE* p = static_cast<const BYTE*>(data);
    size_t remaining = bytes;
    while (remaining) {
        uint32_t wrote = 0;
        const bool write_ok = WriteFile(file, const_cast<BYTE*>(p), static_cast<uint32_t>(remaining), &wrote, 0);
        if (!write_ok || !wrote) {
            DbgPrint("[iPhoneTether360:PLATFORM] file write failed | operation=WriteFile result=%s status=unavailable requested=0x%08x written=0x%08x\n",
                     write_ok ? "TRUE-with-zero-write" : "FALSE",
                     static_cast<unsigned>(remaining), static_cast<unsigned>(wrote));
            return false;
        }
        p += wrote;
        remaining -= wrote;
    }
    return true;
#else
    return fwrite(data, 1, bytes, static_cast<FILE*>(file)) == bytes;
#endif
}

void CloseFile(FileHandle file) {
    if (!file) return;
#if defined(IT360_OPENXECHAIN)
    if (!CloseHandle(file))
        DbgPrint("[iPhoneTether360:PLATFORM] file close failed | operation=CloseHandle result=FALSE status=unavailable\n");
#else
    fclose(static_cast<FILE*>(file));
#endif
}

bool RemoveFile(const char* path) {
    if (!path) return false;
#if defined(IT360_OPENXECHAIN)
    return DeleteFileA(const_cast<char*>(path));
#else
    return remove(path) == 0;
#endif
}

} // namespace it360_platform
