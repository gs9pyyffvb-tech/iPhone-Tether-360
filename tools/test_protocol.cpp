#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/mux_proto.h"
#include "../src/lockdown_probe.h"

int main() {
    uint8_t b[4096];
    size_t n = iphone_mux::BuildVersionRequest(b, sizeof(b));
    assert(n == 20);
    assert(iphone_mux::ReadBe32(b+0) == iphone_mux::kProtoVersion);
    assert(iphone_mux::ReadBe32(b+4) == 20);
    assert(iphone_mux::ReadBe32(b+8) == 2);
    assert(iphone_mux::ReadBe32(b+12) == 0);

    n = iphone_mux::BuildSetupPacket(b, sizeof(b), 0, 0xffff);
    assert(n == 17);
    assert(iphone_mux::ReadBe32(b+0) == iphone_mux::kProtoSetup);
    assert(iphone_mux::ReadBe32(b+4) == 17);
    assert(iphone_mux::ReadBe32(b+8) == iphone_mux::kMuxMagic);
    assert(iphone_mux::ReadBe16(b+12) == 0);
    assert(iphone_mux::ReadBe16(b+14) == 0xffff);
    assert(b[16] == 7);

    n = iphone_mux::BuildTcpPacket(b, sizeof(b), 1, 2, 1, 62078, 0, 0,
                                   iphone_mux::kTcpSyn, 131072, 0, 0);
    assert(n == 36);
    iphone_mux::ParsedMux mux;
    assert(iphone_mux::ParseMux(b, n, true, &mux));
    assert(mux.protocol == iphone_mux::kProtoTcp);
    iphone_mux::ParsedTcp tcp;
    assert(iphone_mux::ParseTcp(mux, &tcp));
    assert(tcp.sourcePort == 1);
    assert(tcp.destPort == 62078);
    assert(tcp.flags == iphone_mux::kTcpSyn);
    assert(tcp.windowEncoded == (131072 >> 8));
    assert(tcp.payloadLength == 0);

    n = lockdown_probe::BuildQueryTypeFrame(b, sizeof(b));
    assert(n > 4);
    uint32_t plen = lockdown_probe::FramedPlistLength(b, n);
    assert(plen == n - 4);
    assert(strstr((const char*)b + 4, "<string>QueryType</string>") != 0);

    const char reply[] = "<plist><dict><key>Type</key><string>com.apple.mobile.lockdown</string></dict></plist>";
    assert(lockdown_probe::ResponseContainsLockdownType((const uint8_t*)reply, sizeof(reply)-1));

    puts("PASS Batch 9A protocol tests");
    return 0;
}
