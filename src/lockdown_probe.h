#pragma once
#include <stddef.h>
#include <stdint.h>

namespace lockdown_probe {

size_t BuildQueryTypeFrame(uint8_t* out, size_t capacity);
bool ResponseContainsLockdownType(const uint8_t* data, size_t length);
uint32_t FramedPlistLength(const uint8_t* data, size_t length);

} // namespace lockdown_probe
