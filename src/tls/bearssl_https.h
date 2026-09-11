#pragma once
#include <stdint.h>
#include "tls/b9c_stream.h"
#include "tls/deadline.h"
namespace it360_tls {
enum HttpsFetchResult {
    HttpsFetchOk = 0,
    HttpsFetchDnsFailed,
    HttpsFetchConnectFailed,
    HttpsFetchTlsFailed,
    HttpsFetchHttpFailed,
    HttpsFetchTooLarge,
    HttpsFetchTimedOut
};
enum HttpsFetchStage {
    HttpsFetchStageNone = 0,
    HttpsFetchStageArguments,
    HttpsFetchStageDns,
    HttpsFetchStageConnect,
    HttpsFetchStageTrustAnchors,
    HttpsFetchStageTlsReset,
    HttpsFetchStageRequestFormat,
    HttpsFetchStageTlsWrite,
    HttpsFetchStageTlsFlush,
    HttpsFetchStageTlsRead,
    HttpsFetchStageHttpHeaders,
    HttpsFetchStageHttpStatus,
    HttpsFetchStageHttpBody
};
struct HttpsFetchDiagnostic {
    HttpsFetchStage stage;
    int32_t status;
    bool has_status;
    HttpsFetchDiagnostic() : stage(HttpsFetchStageNone), status(0), has_status(false) {}
};
const char* HttpsFetchResultName(HttpsFetchResult result);
const char* HttpsFetchStageName(HttpsFetchStage stage);
HttpsFetchResult FetchFixedLicenceFile(B9cTcpStream& stream,
                                       char* body, unsigned body_capacity,
                                       unsigned* body_length,
                                       const Deadline& deadline,
                                       HttpsFetchDiagnostic* diagnostic = 0);
}
