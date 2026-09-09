#pragma once

namespace it360_diag {

// Starts the asynchronous persistent log writer and resolves Xbox notification UI.
// Safe to call more than once.
void Init();
void Shutdown();

// Mirrors to DbgPrint immediately and queues the same formatted line for persistent
// storage. The filesystem write is deliberately performed by the worker thread,
// never by the USB callback that called Log().
void Log(const char* format, ...);

// Queues a short dashboard notification. Notification delivery is best-effort and
// never affects the USB/pairing state machine.
void Notify(const char* text);

// Returns the selected persistent log path, or an empty string until a writable
// Xbox storage volume has been found.
const char* LogPath();

} // namespace it360_diag
