#pragma once
#include <stdint.h>
namespace it360_boot_log {
bool Open(const char* native_path, uint32_t* ntstatus_out = 0);
void Line(const char* text);
void Hex32(const char* prefix, uint32_t value);
void Close();
bool IsOpen();
}
