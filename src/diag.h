#pragma once

namespace it360_diag {

// Immediate crash-diagnostic checkpoint.
// Unlike Log(), this writes synchronously and does not depend on the
// asynchronous logging worker being alive.
void BootCheckpoint(const char* format, ...);

void Init();
void Shutdown();

void Log(const char* format, ...);
void Notify(const char* text);

const char* LogPath();

} // namespace it360_diag
