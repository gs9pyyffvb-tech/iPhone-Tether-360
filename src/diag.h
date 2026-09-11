#pragma once
namespace it360_diag {
enum PhoneNoticeKind {
    PhoneNoticeDetected = 1,
    PhoneNoticeCheckingData,
    PhoneNoticeDataWorking,
    PhoneNoticeDataNotWorking,
    PhoneNoticeLicenceFound,
    PhoneNoticeNoLicence,
    PhoneNoticeComplete,
    PhoneNoticeReconnecting
};
void BootCheckpoint(const char* format, ...);
void Init();
void Shutdown();
void Log(const char* format, ...);
void Notify(const char* text);
void BeginPhoneNotifications(unsigned connection_id);
void EndPhoneNotifications(unsigned connection_id);
void ResetPhoneProgressNotifications(unsigned connection_id);
void NotifyPhone(unsigned connection_id, PhoneNoticeKind kind, const char* text);
const char* LogPath();
}
