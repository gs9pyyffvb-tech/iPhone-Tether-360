#include "shared/app_path.h"
#include "platform/xbox_platform.h"

namespace it360_app_path {
namespace {

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
static bool CopyBounded(const char* source, char* out, size_t capacity) {
    if (!source || !out || capacity < 2) return false;

    size_t i = 0;
    while (source[i]) {
        if (i + 1 >= capacity) {
            out[0] = 0;
            return false;
        }
        out[i] = source[i];
        ++i;
    }
    out[i] = 0;
    return i != 0;
}

#endif

} // namespace

bool ResolveLoadedImagePath(char* out, size_t capacity, it360_platform::ResolveStatus* diagnostic) {
    if (!out || capacity < 2) return false;
    out[0] = 0;

#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    void* image_name_export = 0;
    if (!it360_platform::ResolveModuleOrdinal("xboxkrnl.exe", 431u, &image_name_export, diagnostic) ||
        !image_name_export) {
        return false;
    }

    const char* image_name = static_cast<const char*>(image_name_export);
    if (!image_name[0]) return false;
    return CopyBounded(image_name, out, capacity);
#else
    (void)capacity;
    (void)diagnostic;
    return false;
#endif
}

bool ResolveAppDirectory(char* out, size_t capacity, it360_platform::ResolveStatus* diagnostic) {
    if (!out || capacity < 2) return false;
    out[0] = 0;

    char image_path[512];
    if (!ResolveLoadedImagePath(image_path, sizeof(image_path), diagnostic)) return false;

    size_t length = 0;
    while (image_path[length]) ++length;
    if (!length) return false;

    size_t split = length;
    while (split > 0 && image_path[split - 1] != '\\' && image_path[split - 1] != '/') {
        --split;
    }
    if (split == 0) return false;

    size_t directory_length = split - 1;
    while (directory_length > 0 &&
           (image_path[directory_length - 1] == '\\' || image_path[directory_length - 1] == '/')) {
        --directory_length;
    }

    if (directory_length + 1 > capacity) return false;
    for (size_t i = 0; i < directory_length; ++i) out[i] = image_path[i];
    out[directory_length] = 0;
    return directory_length != 0;
}

bool Join(const char* directory, const char* leaf, char* out, size_t capacity) {
    if (!directory || !leaf || !out || capacity < 2 || !directory[0] || !leaf[0]) return false;

    size_t dlen = 0;
    while (directory[dlen]) ++dlen;
    size_t llen = 0;
    while (leaf[llen]) ++llen;

    const bool needs_sep = directory[dlen - 1] != '\\' && directory[dlen - 1] != '/';
    const size_t total = dlen + (needs_sep ? 1u : 0u) + llen;
    if (total + 1 > capacity) {
        out[0] = 0;
        return false;
    }

    size_t p = 0;
    for (size_t i = 0; i < dlen; ++i) out[p++] = directory[i];
    if (needs_sep) out[p++] = '\\';
    for (size_t i = 0; i < llen; ++i) out[p++] = leaf[i];
    out[p] = 0;
    return true;
}

} // namespace it360_app_path
