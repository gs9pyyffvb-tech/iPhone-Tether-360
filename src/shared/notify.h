#pragma once
namespace it360_notify {
typedef void (*SystemTraceFn)(const char* text);
void SetSystemTrace(SystemTraceFn trace);
bool ShowTitle(const char* ascii_text);
bool ShowSystem(const char* ascii_text);
bool Starting();
bool Ready();
bool AlreadyRunning();
bool LicenceFound();
bool NoLicence();
bool InternetNotFound();
}
