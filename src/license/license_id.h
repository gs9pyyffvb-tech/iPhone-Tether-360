#pragma once
#include <stddef.h>
#include "license/cpu_key.h"
namespace it360_license {
bool DeriveLicenceId(char out_hex[65], CpuKeyDiagnostic* diagnostic = 0);
void SecureZero(void* data, size_t bytes);
}
