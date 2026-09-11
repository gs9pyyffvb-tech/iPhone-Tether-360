#include "shared/boot_log.h"
#include "platform/xbox_platform.h"

namespace it360_boot_log {
namespace {

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
static HANDLE g_file = 0;
#endif

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
static size_t Length(const char* text) {
    if (!text) return 0;
    size_t n = 0;
    while (text[n]) ++n;
    return n;
}
#endif

static char HexDigit(unsigned value) {
    return value < 10u ? static_cast<char>('0' + value)
                       : static_cast<char>('A' + (value - 10u));
}

} // namespace

bool Open(const char* native_path, uint32_t* ntstatus_out) {
    if (ntstatus_out) *ntstatus_out = 0;
    if (!native_path || !native_path[0]) return false;

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    Close();

    ANSI_STRING name;
    name.Length = static_cast<uint16_t>(Length(native_path));
    name.MaximumLength = static_cast<uint16_t>(name.Length + 1u);
    name.Buffer = const_cast<char*>(native_path);

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

    if (ntstatus_out) {
        *ntstatus_out = it360_platform::FailedStatus(status)
            ? static_cast<uint32_t>(status)
            : static_cast<uint32_t>(io.Status);
    }
    if (it360_platform::FailedStatus(status) || it360_platform::FailedStatus(io.Status) || !file) {
        if (!it360_platform::FailedStatus(status) && !it360_platform::FailedStatus(io.Status) && !file) {
            DbgPrint("[iPhoneTether360:BOOTLOG] NtCreateFile returned success with NULL handle | NTSTATUS=0x%08x io_status=0x%08x\n",
                     static_cast<unsigned>(status), static_cast<unsigned>(io.Status));
        } else {
            DbgPrint("[iPhoneTether360:BOOTLOG] NtCreateFile failed NTSTATUS=0x%08x io_status=0x%08x file_null=%u\n",
                     static_cast<unsigned>(status), static_cast<unsigned>(io.Status), file ? 0u : 1u);
        }
        return false;
    }
    g_file = file;
    return true;
#else
    (void)native_path;
    (void)ntstatus_out;
    return true;
#endif
}

void Line(const char* text) {
    if (!text) return;

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    if (!g_file) return;

    const char* pieces[2] = { text, "\r\n" };
    const size_t lengths[2] = { Length(text), 2u };

    for (unsigned p = 0; p < 2; ++p) {
        size_t offset = 0;
        while (offset < lengths[p]) {
            IO_STATUS_BLOCK io;
            io.Status = 0;
            io.Information = 0;
            const DWORD chunk = static_cast<DWORD>(lengths[p] - offset);
            const NTSTATUS status = NtWriteFile(
                g_file, 0, 0, 0, &io,
                const_cast<char*>(pieces[p] + offset),
                chunk, 0
            );
            if (it360_platform::FailedStatus(status) || it360_platform::FailedStatus(io.Status) ||
                io.Information == 0 || io.Information > chunk) {
                DbgPrint("[iPhoneTether360:BOOTLOG] NtWriteFile failed NTSTATUS=0x%08x io_status=0x%08x information=0x%08x requested=0x%08x\n",
                         static_cast<unsigned>(status), static_cast<unsigned>(io.Status),
                         static_cast<unsigned>(io.Information), static_cast<unsigned>(chunk));
                Close();
                return;
            }
            offset += io.Information;
        }
    }

    IO_STATUS_BLOCK flush_io;
    flush_io.Status = 0;
    flush_io.Information = 0;
    const NTSTATUS flush_status = NtFlushBuffersFile(g_file, &flush_io);
    if (it360_platform::FailedStatus(flush_status) || it360_platform::FailedStatus(flush_io.Status)) {
        DbgPrint("[iPhoneTether360:BOOTLOG] NtFlushBuffersFile failed NTSTATUS=0x%08x io_status=0x%08x\n",
                 static_cast<unsigned>(flush_status), static_cast<unsigned>(flush_io.Status));
        Close();
    }
#else
    (void)text;
#endif
}

void Hex32(const char* prefix, uint32_t value) {
    char line[96];
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
    for (int shift = 28; shift >= 0; shift -= 4) {
        line[p++] = HexDigit((value >> shift) & 0xFu);
    }
    line[p] = 0;
    Line(line);
}

void Close() {
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    if (g_file) {
        const NTSTATUS status = NtClose(g_file);
        if (it360_platform::FailedStatus(status))
            DbgPrint("[iPhoneTether360:BOOTLOG] NtClose failed NTSTATUS=0x%08x\n", static_cast<unsigned>(status));
        g_file = 0;
    }
#endif
}

bool IsOpen() {
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    return g_file != 0;
#else
    return true;
#endif
}

} // namespace it360_boot_log
