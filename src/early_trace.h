#pragma once

#include <stddef.h>
#include <stdint.h>

namespace it360_early_trace {

// Before ConfigureApplicationPaths succeeds, writes are synchronously flushed
// to Hdd1:\\iPhoneTether360.log using native xboxkrnl file APIs. Once the Core
// path is safe to resolve, the buffered pre-CRT trace is replayed into the
// app-local Boot.log and all subsequent checkpoints use that file.
bool ConfigureApplicationPaths();
bool BeginSessionLiteral(const char* text);
bool WriteLiteral(const char* text);
bool WriteBuffer(const char* data, size_t length, bool append_crlf);
bool WriteHex32(const char* prefix, uint32_t value);

bool PersistentReady();
bool NormalBootLogReady();
uint32_t FailureCount();
uint32_t LastStatus();

const char* NativePath();
const char* VisiblePath();
const char* EmergencyVisiblePath();
const char* RuntimeLogPath();

} // namespace it360_early_trace
