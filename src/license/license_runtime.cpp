#include "license/license_runtime.h"

#include "diag.h"
#include "license/cpu_key.h"
#include "license/license_id.h"
#include "tls/b9c_stream.h"
#include "tls/bearssl_https.h"
#include "tls/deadline.h"

#include <string.h>

namespace it360_license_runtime {
namespace {

struct Runtime {
    Runtime()
        : state(LicenceUnknown), gate(NetworkGatePending), check_active(0),
          current_connection(0), last_attempt_connection(0), check_connection(0),
          independent_internet_ok(0) {}

    volatile LONG state;
    volatile LONG gate;
    volatile LONG check_active;
    volatile LONG current_connection;
    volatile LONG last_attempt_connection;
    volatile LONG check_connection;
    volatile LONG* independent_internet_ok;
    it360_tls::B9cTcpStream stream;
};

static Runtime& R() {
    static Runtime runtime;
    return runtime;
}

static void SetStateAndGate(LicenceState state, NetworkGate gate) {
    Runtime& r = R();
    it360_platform::AtomicExchange(&r.state, static_cast<LONG>(state));
    it360_platform::AtomicExchange(&r.gate, static_cast<LONG>(gate));
}

static bool SameCurrentConnection(unsigned id) {
    return static_cast<unsigned>(it360_platform::AtomicCompareExchange(&R().current_connection, 0, 0)) == id;
}

static bool IndependentInternetWorks(volatile LONG* flag) {
    return flag && it360_platform::AtomicCompareExchange(flag, 0, 0) != 0;
}

static void FinishUnavailable(unsigned connection_id, const it360_tls::Deadline& deadline) {
    Runtime& r = R();

    // The independent example.com probe runs concurrently in StandaloneNetStack.
    // If the HTTPS path fails early, give that probe the unused portion of the
    // same six-second budget. Never extend the licence decision beyond it.
    while (SameCurrentConnection(connection_id) && !deadline.Expired() &&
           !IndependentInternetWorks(r.independent_internet_ok)) {
        it360_platform::SleepMs(10);
    }

    // A stale attempt must not change the gate or notify for a newer phone.
    if (!SameCurrentConnection(connection_id)) {
        it360_diag::Log("LICENCE | Previous connection check ended without a decision\n");
        return;
    }

    SetStateAndGate(LicenceUnknown, NetworkGateAllow);
    if (IndependentInternetWorks(r.independent_internet_ok)) {
        it360_diag::Log("LICENCE | Check unavailable\n");
        it360_diag::Log("LICENCE | Fail-open access allowed\n");
    } else {
        it360_diag::Log("NETWORK | Internet not found\n");
        it360_diag::Log("LICENCE | Fail-open access allowed\n");
        it360_diag::NotifyPhone(connection_id, it360_diag::PhoneNoticeDataNotWorking,
                              "iPhone Data Not Working");
    }
}

static uint32_t CheckWorker(void*) {
    Runtime& r = R();
    const unsigned connection_id = static_cast<unsigned>(
        it360_platform::AtomicCompareExchange(&r.check_connection, 0, 0));

    // This deadline is created once and is passed into HTTPS. DNS, TCP, TLS,
    // HTTP and the independent-connectivity classification all share it.
    const it360_tls::Deadline deadline(6000u);

    char licence_id[65];
    char body[8192];
    licence_id[0] = 0;
    body[0] = 0;

    it360_license::CpuKeyDiagnostic cpu_diagnostic;
    if (!it360_license::DeriveLicenceId(licence_id, &cpu_diagnostic)) {
        if (cpu_diagnostic.failure == it360_license::CpuKeyFailureResolveExpansionCall &&
            cpu_diagnostic.resolve.has_status) {
            it360_diag::Log("LICENCE | Local console ID unavailable | operation=%s/%s NTSTATUS=0x%08x\n",
                            it360_license::CpuKeyFailureName(cpu_diagnostic.failure),
                            it360_platform::ResolveOperationName(cpu_diagnostic.resolve.operation),
                            static_cast<unsigned>(cpu_diagnostic.resolve.status));
        } else {
            it360_diag::Log("LICENCE | Local console ID unavailable | operation=%s status=unavailable\n",
                            it360_license::CpuKeyFailureName(cpu_diagnostic.failure));
        }
        if (SameCurrentConnection(connection_id)) {
            SetStateAndGate(LicenceUnknown, NetworkGateAllow);
            it360_diag::Log("LICENCE | Fail-open access allowed\n");
        }
        it360_license::SecureZero(licence_id, sizeof(licence_id));
        it360_platform::AtomicExchange(&r.check_active, 0);
        return 0;
    }

    unsigned body_length = 0;
    it360_tls::HttpsFetchDiagnostic fetch_diagnostic;
    const it360_tls::HttpsFetchResult fetch = it360_tls::FetchFixedLicenceFile(
        r.stream, body, sizeof(body) - 1u, &body_length, deadline, &fetch_diagnostic);

    if (fetch != it360_tls::HttpsFetchOk) {
        if (fetch_diagnostic.has_status) {
            it360_diag::Log("LICENCE | HTTPS check failed | result=%u (%s) stage=%s status=0x%08x\n",
                            static_cast<unsigned>(fetch), it360_tls::HttpsFetchResultName(fetch),
                            it360_tls::HttpsFetchStageName(fetch_diagnostic.stage),
                            static_cast<unsigned>(fetch_diagnostic.status));
        } else {
            it360_diag::Log("LICENCE | HTTPS check failed | result=%u (%s) stage=%s status=unavailable\n",
                            static_cast<unsigned>(fetch), it360_tls::HttpsFetchResultName(fetch),
                            it360_tls::HttpsFetchStageName(fetch_diagnostic.stage));
        }
    }

    if (fetch == it360_tls::HttpsFetchOk) {
        if (SameCurrentConnection(connection_id) && r.independent_internet_ok) {
            it360_platform::AtomicExchange(r.independent_internet_ok, 1);
            it360_diag::NotifyPhone(connection_id, it360_diag::PhoneNoticeDataWorking,
                                    "iPhone Data Working");
        }
        const LicenceDocumentVerdict verdict = EvaluateLicenceDocument(body, body_length, licence_id);
        if (verdict == LicenceDocumentMatched) {
            SetStateAndGate(LicenceLicensed, NetworkGateAllow);
            it360_diag::Log("LICENCE | Console licence matched\n");
            it360_diag::NotifyPhone(connection_id, it360_diag::PhoneNoticeLicenceFound,
                                  "License Has Been Found");
        } else if (verdict == LicenceDocumentFree) {
            SetStateAndGate(LicenceFree, NetworkGateAllow);
            it360_diag::Log("LICENCE | Free mode enabled\n");
        } else if (verdict == LicenceDocumentNoMatch) {
            SetStateAndGate(LicenceUnlicensed, NetworkGateDeny);
            it360_diag::Log("LICENCE | Valid list received; console not listed\n");
            it360_diag::NotifyPhone(connection_id, it360_diag::PhoneNoticeNoLicence,
                                  "No License found for this Xbox");
        } else {
            // A 200 response that is not a recognisable allow-list is not proof
            // that the console is unlicensed. Treat it as service unavailable.
            it360_diag::Log("LICENCE | Response was not a recognised list\n");
            FinishUnavailable(connection_id, deadline);
        }
    } else {
        FinishUnavailable(connection_id, deadline);
    }

    it360_license::SecureZero(licence_id, sizeof(licence_id));
    it360_license::SecureZero(body, sizeof(body));
    r.stream.Close();
    it360_platform::AtomicExchange(&r.check_active, 0);
    return 0;
}

} // namespace

void ResetCoreSession() {
    Runtime& r = R();
    if (it360_platform::AtomicCompareExchange(&r.check_active, 0, 0) != 0) return;
    r.stream.Reset();
    r.independent_internet_ok = 0;
    it360_platform::AtomicExchange(&r.current_connection, 0);
    it360_platform::AtomicExchange(&r.last_attempt_connection, 0);
    it360_platform::AtomicExchange(&r.check_connection, 0);
    SetStateAndGate(LicenceUnknown, NetworkGatePending);
}

void BeginPhoneConnection(unsigned connection_id) {
    if (!connection_id) return;
    Runtime& r = R();
    it360_platform::AtomicExchange(&r.current_connection, static_cast<LONG>(connection_id));

    const LicenceState state = State();
    if (state == LicenceLicensed || state == LicenceFree)
        it360_platform::AtomicExchange(&r.gate, NetworkGateAllow);
    else if (state == LicenceUnlicensed)
        it360_platform::AtomicExchange(&r.gate, NetworkGateDeny);
    else
        it360_platform::AtomicExchange(&r.gate, NetworkGatePending);
}

void EndPhoneConnection(unsigned connection_id) {
    if (!connection_id) return;
    Runtime& r = R();
    if (!SameCurrentConnection(connection_id)) return;
    it360_platform::AtomicExchange(&r.current_connection, 0);
    if (State() == LicenceUnknown)
        it360_platform::AtomicExchange(&r.gate, NetworkGatePending);
}

bool MaybeStartCheck(unsigned connection_id, const it360_net::NetConfig& config,
                     const BYTE gateway_mac[6], it360_net::FrameSendFn send,
                     void* send_user, volatile LONG* independent_internet_ok) {
    Runtime& r = R();
    if (!connection_id || !SameCurrentConnection(connection_id)) return false;

    const LicenceState state = State();
    if (state != LicenceUnknown) return false;
    if (it360_platform::AtomicCompareExchange(&r.check_active, 0, 0) != 0) return false;
    if (static_cast<unsigned>(it360_platform::AtomicCompareExchange(&r.last_attempt_connection, 0, 0)) == connection_id)
        return false;

    if (!r.stream.Configure(config, gateway_mac, send, send_user)) {
        it360_diag::Log("LICENCE | Check setup failed | operation=B9cTcpStream::Configure result=0x00000000 status=unavailable\n");
        return false;
    }

    r.independent_internet_ok = independent_internet_ok;
    it360_platform::AtomicExchange(&r.check_connection, static_cast<LONG>(connection_id));
    it360_platform::AtomicExchange(&r.last_attempt_connection, static_cast<LONG>(connection_id));
    it360_platform::AtomicExchange(&r.gate, NetworkGatePending);
    it360_platform::AtomicExchange(&r.check_active, 1);

    it360_diag::Log("LICENCE | Check started\n");
    it360_platform::ThreadStartStatus thread_diagnostic;
    if (!it360_platform::StartDetachedThread(&CheckWorker, 0, &thread_diagnostic)) {
        it360_platform::AtomicExchange(&r.check_active, 0);
        SetStateAndGate(LicenceUnknown, NetworkGateAllow);
        if (thread_diagnostic.has_status) {
            it360_diag::Log("LICENCE | Check unavailable | operation=%s NTSTATUS=0x%08x\n",
                            it360_platform::ThreadStartOperationName(thread_diagnostic.operation),
                            static_cast<unsigned>(thread_diagnostic.status));
        } else {
            it360_diag::Log("LICENCE | Check unavailable | operation=%s status=unavailable\n",
                            it360_platform::ThreadStartOperationName(thread_diagnostic.operation));
        }
        it360_diag::Log("LICENCE | Fail-open access allowed\n");
        return false;
    }
    if (thread_diagnostic.close_status_valid && it360_platform::FailedStatus(thread_diagnostic.close_status))
        it360_diag::Log("LICENCE | Check thread handle cleanup failed | operation=NtClose NTSTATUS=0x%08x\n",
                        static_cast<unsigned>(thread_diagnostic.close_status));
    return true;
}

bool OnEthernetFrame(const BYTE* frame, unsigned length) {
    Runtime& r = R();
    if (it360_platform::AtomicCompareExchange(&r.check_active, 0, 0) == 0) return true;
    return r.stream.OnEthernetFrame(frame, length);
}

LicenceState State() {
    return static_cast<LicenceState>(it360_platform::AtomicCompareExchange(&R().state, 0, 0));
}

NetworkGate Gate() {
    return static_cast<NetworkGate>(it360_platform::AtomicCompareExchange(&R().gate, 0, 0));
}

bool CheckActive() {
    return it360_platform::AtomicCompareExchange(&R().check_active, 0, 0) != 0;
}

bool NativeNetworkAllowed() { return Gate() == NetworkGateAllow; }

} // namespace it360_license_runtime
