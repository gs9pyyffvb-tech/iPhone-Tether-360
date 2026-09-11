#pragma once

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#if defined(IT360_OPENXECHAIN)
// The pinned xecorelib revision has a typo in xboxkrnl_string.h and
// xboxkrnl_misc.h: both open their C-linkage guard with __cplusplus but close
// it with ___cplusplus. Define the misspelled guard only while importing
// xecorelib so those two headers close their own extern "C" blocks correctly.
#if defined(__cplusplus) && !defined(___cplusplus)
#define IT360_XECORELIB_CPLUSPLUS_GUARD_WORKAROUND 1
#define ___cplusplus 1
#endif

#include <xecore/xboxkrnl.h>
#include <xecore/xam.h>

#if defined(IT360_XECORELIB_CPLUSPLUS_GUARD_WORKAROUND)
#undef ___cplusplus
#undef IT360_XECORELIB_CPLUSPLUS_GUARD_WORKAROUND
#endif
#else
// Host-only declarations used by the source syntax audit. They intentionally
// mirror the OpenXeChain/xecorelib ABI surface, not the proprietary SDK headers.
typedef void* HANDLE;
typedef void* HMODULE;
typedef uint32_t NTSTATUS;
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif
extern "C" void DbgPrint(const char* format, ...);
#endif

// Project-local fixed-width aliases. The Xbox 360 target is 32-bit even when
// these sources are syntax-checked on a 64-bit host.
typedef uint8_t  BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t  LONG;
typedef uint64_t ULONGLONG;
typedef uint16_t WCHAR;

#if defined(__GNUC__) || defined(__clang__)
#define IT360_ALIGN8 __attribute__((aligned(8)))
#if defined(IT360_OPENXECHAIN)
#define IT360_EXEC_SECTION __attribute__((section(".text"), aligned(16)))
#else
#define IT360_EXEC_SECTION __attribute__((aligned(16)))
#endif
#define IT360_STATIC_ASSERT(name, expr) typedef char it360_static_assert_##name[(expr) ? 1 : -1]
#else
#define IT360_ALIGN8
#define IT360_EXEC_SECTION
#define IT360_STATIC_ASSERT(name, expr) typedef char it360_static_assert_##name[(expr) ? 1 : -1]
#endif

namespace it360_platform {

typedef uint32_t (*ThreadFn)(void* context);

enum ResolveOperation {
    ResolveOperationNone = 0,
    ResolveOperationInvalidArgument,
    ResolveOperationXexGetModuleHandle,
    ResolveOperationNullModuleHandle,
    ResolveOperationXexGetProcedureAddress,
    ResolveOperationNullProcedureAddress
};

struct ResolveStatus {
    ResolveOperation operation;
    NTSTATUS status;
    bool has_status;
    ResolveStatus() : operation(ResolveOperationNone), status(0), has_status(false) {}
};

enum ThreadStartOperation {
    ThreadStartOperationNone = 0,
    ThreadStartOperationInvalidEntry,
    ThreadStartOperationExAllocatePoolWithTag,
    ThreadStartOperationExCreateThread,
    ThreadStartOperationNullThreadHandle
};

struct ThreadStartStatus {
    ThreadStartOperation operation;
    NTSTATUS status;
    bool has_status;
    NTSTATUS close_status;
    bool close_status_valid;
    ThreadStartStatus()
        : operation(ThreadStartOperationNone), status(0), has_status(false),
          close_status(0), close_status_valid(false) {}
};

template <typename T>
inline DWORD Address32(T address) {
    return static_cast<DWORD>(reinterpret_cast<uintptr_t>(address));
}

bool FailedStatus(NTSTATUS status);
const char* ResolveOperationName(ResolveOperation operation);
const char* ThreadStartOperationName(ThreadStartOperation operation);
LONG AtomicCompareExchange(volatile LONG* value, LONG exchange, LONG comparand);
LONG AtomicExchange(volatile LONG* value, LONG exchange);
LONG AtomicIncrement(volatile LONG* value);
void SleepMs(DWORD milliseconds);
uint64_t MonotonicMs();
void FlushInstructionCache(void* address, size_t length);
bool StartDetachedThread(ThreadFn entry, void* context, ThreadStartStatus* diagnostic = 0);

bool ResolveModuleOrdinal(const char* module_name, DWORD ordinal, void** out,
                          ResolveStatus* diagnostic = 0);
WORD KernelBuild(ResolveStatus* diagnostic = 0);

// File helpers use the Xbox OS imports exposed by OpenXeChain/xecorelib on
// target, keeping filesystem work independent from legacy SDK CRT wrappers.
typedef void* FileHandle;
FileHandle OpenAppend(const char* path);
FileHandle OpenRead(const char* path);
FileHandle OpenTruncate(const char* path);
bool ReadBounded(FileHandle file, void* data, size_t capacity, size_t* bytes_read);
bool WriteAll(FileHandle file, const void* data, size_t bytes);
void CloseFile(FileHandle file);
bool RemoveFile(const char* path);

} // namespace it360_platform
