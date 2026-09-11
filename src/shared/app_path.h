#pragma once
#include <stddef.h>
#include "platform/xbox_platform.h"
namespace it360_app_path {
bool ResolveLoadedImagePath(char* out, size_t capacity,
                            it360_platform::ResolveStatus* diagnostic = 0);
bool ResolveAppDirectory(char* out, size_t capacity,
                         it360_platform::ResolveStatus* diagnostic = 0);
bool Join(const char* directory, const char* leaf, char* out, size_t capacity);
}
