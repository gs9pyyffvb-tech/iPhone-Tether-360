#include "native_net/xnet_bridge.h"
#include "platform/xbox_platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {
typedef int (*TxCb)(void*, const BYTE*, DWORD);
typedef int (*RxCb)(void*, const BYTE*, DWORD);
static TxCb gTx = 0;
static RxCb gRx = 0;
static void* gUser = 0;
static unsigned gRegistrations = 0;
static BYTE gInjected[1514];
static unsigned gInjectedLen = 0;

static int MockSetCallbacks(DWORD caller, TxCb tx, RxCb rx, void* user, DWORD flags) {
    if (caller != 2u || !tx || !rx || flags != 0u) return -1;
    gTx = tx; gRx = rx; gUser = user; ++gRegistrations; return 0;
}
static int MockXmit(DWORD, BYTE*, DWORD) { return 0; }
static void MockRecv(DWORD caller, BYTE* data, DWORD length) {
    if (caller != 2u || !data || length > sizeof(gInjected)) return;
    memcpy(gInjected, data, length); gInjectedLen = length;
}
static DWORD MockLink(DWORD caller) { return caller == 2u ? 0x0Bu : 0u; }

static void W16(BYTE* p, WORD v) { p[0]=(BYTE)(v>>8); p[1]=(BYTE)v; }
static int Fail(const char* why) { fprintf(stderr,"STEP17 BRIDGE TEST FAIL: %s\n",why); return 1; }
}

namespace it360_platform {
bool FailedStatus(NTSTATUS status) { return (int32_t)status < 0; }
LONG AtomicCompareExchange(volatile LONG* value, LONG exchange, LONG comparand) {
    return __sync_val_compare_and_swap(value, comparand, exchange);
}
LONG AtomicExchange(volatile LONG* value, LONG exchange) { return __sync_lock_test_and_set(value, exchange); }
LONG AtomicIncrement(volatile LONG* value) { return __sync_add_and_fetch(value, 1); }
void SleepMs(DWORD) {}
void FlushInstructionCache(void*, size_t) {}
const char* ResolveOperationName(ResolveOperation operation) {
    switch (operation) {
    case ResolveOperationNone: return "none";
    case ResolveOperationInvalidArgument: return "invalid argument";
    case ResolveOperationXexGetModuleHandle: return "XexGetModuleHandle";
    case ResolveOperationNullModuleHandle: return "XexGetModuleHandle(null module)";
    case ResolveOperationXexGetProcedureAddress: return "XexGetProcedureAddress";
    case ResolveOperationNullProcedureAddress: return "XexGetProcedureAddress(null procedure)";
    default: return "unknown";
    }
}
const char* ThreadStartOperationName(ThreadStartOperation) { return "mock"; }
bool StartDetachedThread(ThreadFn, void*, ThreadStartStatus*) { return false; }
bool ResolveModuleOrdinal(const char* module, DWORD ordinal, void** out, ResolveStatus* diagnostic) {
    if (diagnostic) *diagnostic = ResolveStatus();
    if (!module || strcmp(module,"xam.xex") || !out) {
        if (diagnostic) diagnostic->operation = ResolveOperationInvalidArgument;
        return false;
    }
    *out = 0;
    if (ordinal == 75) *out = (void*)&MockLink;
    else if (ordinal == 109) *out = (void*)&MockSetCallbacks;
    else if (ordinal == 110) *out = (void*)&MockXmit;
    else if (ordinal == 111) *out = (void*)&MockRecv;
    if (!*out && diagnostic) diagnostic->operation = ResolveOperationNullProcedureAddress;
    return *out != 0;
}
WORD KernelBuild(ResolveStatus*) { return 17559; }
FileHandle OpenAppend(const char*) { return 0; }
FileHandle OpenRead(const char*) { return 0; }
FileHandle OpenTruncate(const char*) { return 0; }
bool ReadBounded(FileHandle,void*,size_t,size_t*) { return false; }
bool WriteAll(FileHandle,const void*,size_t) { return false; }
void CloseFile(FileHandle) {}
bool RemoveFile(const char*) { return false; }
}

namespace it360_diag {
void BootCheckpoint(const char*, ...) {}
void Init() {}
void Shutdown() {}
void Log(const char*, ...) {}
void Notify(const char*) {}
const char* LogPath() { return ""; }
}

int main() {
    const BYTE nativeMac[6]={0x00,0x11,0x22,0x33,0x44,0x55};
    const BYTE tetherMac[6]={0x02,0xAA,0xBB,0xCC,0xDD,0xEE};
    const BYTE gateway[6]={0x10,0x20,0x30,0x40,0x50,0x60};
    const BYTE bcast[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    if (!it360_native_net::InitializeBridge()) return Fail("initialization");
    it360_native_net::UpdateTransportState(false,true,tetherMac);
    it360_native_net::UpdateTransportState(true,false,tetherMac);
    if (gRegistrations != 0) return Fail("registered before transport+licence allow");
    it360_native_net::UpdateTransportState(true,true,tetherMac);
    if (gRegistrations != 1 || !it360_native_net::IsRegistered() || !it360_native_net::IsActive() || !gTx)
        return Fail("activation/registration");

    BYTE arp[42]; memset(arp,0,sizeof(arp));
    memcpy(arp,bcast,6); memcpy(arp+6,nativeMac,6); W16(arp+12,0x0806);
    BYTE* a=arp+14; W16(a,1); W16(a+2,0x0800); a[4]=6; a[5]=4; W16(a+6,1); memcpy(a+8,nativeMac,6);
    if (gTx(gUser,arp,sizeof(arp)) != 0) return Fail("TX callback result");
    if (!it360_native_net::HasNativeMac()) return Fail("native MAC capture");

    BYTE out[1514]; unsigned outLen=0;
    if (!it360_native_net::PopXboxTransmit(out,sizeof(out),&outLen) || outLen != sizeof(arp))
        return Fail("TX queue drain");
    if (memcmp(out+6,tetherMac,6) || memcmp(out+14+8,tetherMac,6))
        return Fail("worker-side outbound translation");

    BYTE reply[42]; memset(reply,0,sizeof(reply));
    memcpy(reply,tetherMac,6); memcpy(reply+6,gateway,6); W16(reply+12,0x0806);
    a=reply+14; W16(a,1); W16(a+2,0x0800); a[4]=6; a[5]=4; W16(a+6,2);
    memcpy(a+8,gateway,6); memcpy(a+18,tetherMac,6);
    if (!it360_native_net::InjectIphoneFrame(reply,sizeof(reply))) return Fail("inbound injection");
    if (gInjectedLen != sizeof(reply) || memcmp(gInjected,nativeMac,6) || memcmp(gInjected+14+18,nativeMac,6))
        return Fail("inbound translation/injection");

    it360_native_net::PhoneDetached();
    if (it360_native_net::IsActive()) return Fail("detach did not pause");
    if (gTx(gUser,arp,sizeof(arp)) != 0) return Fail("inactive callback result");
    outLen=0;
    if (it360_native_net::PopXboxTransmit(out,sizeof(out),&outLen)) return Fail("inactive TX was queued");

    puts("STEP17 BRIDGE TEST PASS");
    return 0;
}
