#pragma once
#include <stddef.h>
#include <stdint.h>
namespace b64 {
size_t Encode(const uint8_t* in, size_t n, char* out, size_t cap, bool wrap64=false);
size_t Decode(const char* in, size_t n, uint8_t* out, size_t cap);
}
