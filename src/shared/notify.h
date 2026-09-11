#pragma once
namespace it360_notify {
bool Show(const char* ascii_text);
bool Starting();
bool Ready();
bool AlreadyRunning();
bool LicenceFound();
bool NoLicence();
bool InternetNotFound();
}
