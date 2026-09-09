#include "lockdown_probe.h"
#include "mux_proto.h"
#include <string.h>

namespace lockdown_probe {

static const char kQueryTypeXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
    "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"
    "<plist version=\"1.0\"><dict>"
    "<key>Label</key><string>iPhoneTether360</string>"
    "<key>Request</key><string>QueryType</string>"
    "</dict></plist>";

size_t BuildQueryTypeFrame(uint8_t* out, size_t capacity) {
    const size_t xmlLen = sizeof(kQueryTypeXml) - 1;
    if (!out || capacity < 4 + xmlLen) return 0;
    iphone_mux::WriteBe32(out, (uint32_t)xmlLen);
    memcpy(out + 4, kQueryTypeXml, xmlLen);
    return 4 + xmlLen;
}

uint32_t FramedPlistLength(const uint8_t* data, size_t length) {
    if (!data || length < 4) return 0;
    return iphone_mux::ReadBe32(data);
}

static bool Contains(const uint8_t* data, size_t length, const char* needle) {
    const size_t n = strlen(needle);
    if (!data || !needle || n == 0 || length < n) return false;
    for (size_t i = 0; i + n <= length; ++i) {
        if (memcmp(data + i, needle, n) == 0) return true;
    }
    return false;
}

bool ResponseContainsLockdownType(const uint8_t* data, size_t length) {
    return Contains(data, length, "com.apple.mobile.lockdown");
}

} // namespace lockdown_probe
