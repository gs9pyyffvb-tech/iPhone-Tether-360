#include "pair_store.h"
#include "platform/xbox_platform.h"
#include <stdio.h>
#include <string.h>

namespace it360 {
namespace {

static const char kMagic[] = "IT360PAIR1";

static bool Append(char*& p, size_t& left, const char* s) {
    if (!s) return false;
    const size_t n = strlen(s);
    if (n + 1 > left) return false;
    memcpy(p, s, n);
    p += n;
    left -= n;
    *p = 0;
    return true;
}

static bool AddLine(char*& p, size_t& left, const char* key, const char* value) {
    return Append(p,left,key) && Append(p,left,"=") && Append(p,left,value ? value : "") &&
           Append(p,left,"\n");
}

static const char* FindLineValue(const char* text, const char* key, size_t* value_len) {
    if (!text || !key || !value_len) return 0;
    const size_t kn = strlen(key);
    const char* p = text;
    while (*p) {
        const char* e = strchr(p, '\n');
        if (!e) e = p + strlen(p);
        if ((size_t)(e - p) > kn && !memcmp(p, key, kn) && p[kn] == '=') {
            *value_len = (size_t)(e - (p + kn + 1));
            return p + kn + 1;
        }
        if (!*e) break;
        p = e + 1;
    }
    return 0;
}

static bool GetLine(const char* text, const char* key, char* dst, size_t cap, bool required) {
    size_t n = 0;
    const char* v = FindLineValue(text, key, &n);
    if (!v) {
        if (!required && dst && cap) { dst[0] = 0; return true; }
        return false;
    }
    if (!dst || n + 1 > cap) return false;
    memcpy(dst, v, n);
    dst[n] = 0;
    return required ? n != 0 : true;
}

static void SanitizeId(const char* udid, char* out, size_t cap) {
    if (!out || !cap) return;
    size_t o = 0;
    if (udid) {
        for (const char* p = udid; *p && o + 1 < cap; ++p) {
            const char c = *p;
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '_';
            if (ok) out[o++] = c;
        }
    }
    if (!o && cap > 1) out[o++] = '0';
    out[o] = 0;
}

static bool BuildPath(const char* root, const char* udid, char* out, size_t cap) {
    if (!root || !out || cap < 16) return false;
    char safe[96];
    SanitizeId(udid, safe, sizeof(safe));
    const char prefix[] = "iPhoneTether360-";
    const char suffix[] = ".pair";
    const size_t rn = strlen(root), pn = sizeof(prefix)-1, sn = strlen(safe), xn = sizeof(suffix)-1;
    if (rn + pn + sn + xn + 1 > cap) return false;
    char* p = out;
    memcpy(p, root, rn); p += rn;
    memcpy(p, prefix, pn); p += pn;
    memcpy(p, safe, sn); p += sn;
    memcpy(p, suffix, xn); p += xn;
    *p = 0;
    return true;
}

static bool ReadStoredFile(const char* path, char* dst, size_t cap) {
    if (!path || !dst || cap < 2) return false;
#ifdef IT360_XBOX
    it360_platform::FileHandle f = it360_platform::OpenRead(path);
    if (!f) return false;
    size_t n = 0;
    const bool ok = it360_platform::ReadBounded(f, dst, cap - 1, &n);
    it360_platform::CloseFile(f);
    if (!ok || !n || n >= cap) return false;
    dst[n] = 0;
    return true;
#else
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    const size_t n = fread(dst, 1, cap - 1, f);
    const bool eof_ok = feof(f) != 0;
    fclose(f);
    if (!eof_ok || !n) return false;
    dst[n] = 0;
    return true;
#endif
}

static bool WriteStoredFile(const char* path, const char* data, size_t n) {
    if (!path || !data || !n) return false;
#ifdef IT360_XBOX
    it360_platform::FileHandle f = it360_platform::OpenTruncate(path);
    if (!f) return false;
    const bool ok = it360_platform::WriteAll(f, data, n);
    it360_platform::CloseFile(f);
    return ok;
#else
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const size_t wrote = fwrite(data, 1, n, f);
    const int close_result = fclose(f);
    return wrote == n && close_result == 0;
#endif
}

static unsigned RootCount() {
#ifdef IT360_XBOX
    return 5;
#else
    return 1;
#endif
}

static const char* RootAt(unsigned i) {
#ifdef IT360_XBOX
    static const char* const roots[] = { "Hdd1:\\", "Usb0:\\", "Usb1:\\", "Usb2:\\", "Usb3:\\" };
    return i < 5 ? roots[i] : 0;
#else
    return i == 0 ? "/tmp/" : 0;
#endif
}

} // namespace

void ClearStoredPairRecord(StoredPairRecord& record) {
    memset(&record, 0, sizeof(record));
}

size_t SerializePairRecord(const StoredPairRecord& r, char* dst, size_t cap) {
    if (!dst || cap < 2 || !r.udid[0] || !r.identity.host_id[0] || !r.identity.system_buid[0])
        return 0;
    char* p = dst;
    size_t left = cap;
    *p = 0;
    if (!Append(p,left,kMagic) || !Append(p,left,"\n")) return 0;
    if (!AddLine(p,left,"UDID",r.udid)) return 0;
    if (!AddLine(p,left,"HostID",r.identity.host_id)) return 0;
    if (!AddLine(p,left,"SystemBUID",r.identity.system_buid)) return 0;
    if (!AddLine(p,left,"RootCertificate",r.identity.root_certificate_b64)) return 0;
    if (!AddLine(p,left,"RootPrivateKey",r.identity.root_private_key_b64)) return 0;
    if (!AddLine(p,left,"HostCertificate",r.identity.host_certificate_b64)) return 0;
    if (!AddLine(p,left,"HostPrivateKey",r.identity.host_private_key_b64)) return 0;
    if (!AddLine(p,left,"DeviceCertificate",r.identity.device_certificate_b64)) return 0;
    if (!AddLine(p,left,"EscrowBag",r.escrow_bag_b64)) return 0;
    if (!AddLine(p,left,"WiFiAddress",r.wifi_address)) return 0;
    return (size_t)(p - dst);
}

bool DeserializePairRecord(const char* text, StoredPairRecord& r) {
    ClearStoredPairRecord(r);
    if (!text || strncmp(text, kMagic, sizeof(kMagic) - 1) != 0 || text[sizeof(kMagic)-1] != '\n')
        return false;
    if (!GetLine(text,"UDID",r.udid,sizeof(r.udid),true)) return false;
    if (!GetLine(text,"HostID",r.identity.host_id,sizeof(r.identity.host_id),true)) return false;
    if (!GetLine(text,"SystemBUID",r.identity.system_buid,sizeof(r.identity.system_buid),true)) return false;
    if (!GetLine(text,"RootCertificate",r.identity.root_certificate_b64,sizeof(r.identity.root_certificate_b64),true)) return false;
    if (!GetLine(text,"RootPrivateKey",r.identity.root_private_key_b64,sizeof(r.identity.root_private_key_b64),true)) return false;
    if (!GetLine(text,"HostCertificate",r.identity.host_certificate_b64,sizeof(r.identity.host_certificate_b64),true)) return false;
    if (!GetLine(text,"HostPrivateKey",r.identity.host_private_key_b64,sizeof(r.identity.host_private_key_b64),true)) return false;
    if (!GetLine(text,"DeviceCertificate",r.identity.device_certificate_b64,sizeof(r.identity.device_certificate_b64),true)) return false;
    if (!GetLine(text,"EscrowBag",r.escrow_bag_b64,sizeof(r.escrow_bag_b64),false)) return false;
    if (!GetLine(text,"WiFiAddress",r.wifi_address,sizeof(r.wifi_address),false)) return false;
    return true;
}

bool SavePairRecord(const StoredPairRecord& r) {
    static char text[kPairRecordTextCap];
    const size_t n = SerializePairRecord(r, text, sizeof(text));
    if (!n) return false;
    char path[256];
    for (unsigned i = 0; i < RootCount(); ++i) {
        const char* root = RootAt(i);
        if (root && BuildPath(root, r.udid, path, sizeof(path)) && WriteStoredFile(path, text, n))
            return true;
    }
    return false;
}

bool LoadPairRecord(const char* udid, StoredPairRecord& r) {
    if (!udid || !*udid) return false;
    static char text[kPairRecordTextCap];
    char path[256];
    for (unsigned i = 0; i < RootCount(); ++i) {
        const char* root = RootAt(i);
        if (!root || !BuildPath(root, udid, path, sizeof(path))) continue;
        if (ReadStoredFile(path, text, sizeof(text)) && DeserializePairRecord(text, r) && !strcmp(r.udid, udid))
            return true;
    }
    ClearStoredPairRecord(r);
    return false;
}

bool DeletePairRecord(const char* udid) {
    if (!udid || !*udid) return false;
    bool removed = false;
    char path[256];
    for (unsigned i = 0; i < RootCount(); ++i) {
        const char* root = RootAt(i);
        if (root && BuildPath(root, udid, path, sizeof(path))) {
#ifdef IT360_XBOX
            if (it360_platform::RemoveFile(path)) removed = true;
#else
            if (remove(path) == 0) removed = true;
#endif
        }
    }
    return removed;
}

} // namespace it360
