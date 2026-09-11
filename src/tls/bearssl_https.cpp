#include "tls/bearssl_https.h"
#include "tls/sectigo_roots.h"
#include "platform/xbox_platform.h"
#include "license/cpu_key.h"
#include "bearssl.h"
#include <string.h>
#include <stdio.h>
namespace it360_tls {

const char* HttpsFetchResultName(HttpsFetchResult result) {
    switch (result) {
    case HttpsFetchOk: return "ok";
    case HttpsFetchDnsFailed: return "dns failed";
    case HttpsFetchConnectFailed: return "connect failed";
    case HttpsFetchTlsFailed: return "tls failed";
    case HttpsFetchHttpFailed: return "http failed";
    case HttpsFetchTooLarge: return "response too large";
    case HttpsFetchTimedOut: return "timed out";
    default: return "unknown";
    }
}

const char* HttpsFetchStageName(HttpsFetchStage stage) {
    switch (stage) {
    case HttpsFetchStageNone: return "none";
    case HttpsFetchStageArguments: return "arguments";
    case HttpsFetchStageDns: return "DNS";
    case HttpsFetchStageConnect: return "TCP connect";
    case HttpsFetchStageTrustAnchors: return "trust anchors";
    case HttpsFetchStageTlsReset: return "TLS reset";
    case HttpsFetchStageRequestFormat: return "HTTP request format";
    case HttpsFetchStageTlsWrite: return "TLS write";
    case HttpsFetchStageTlsFlush: return "TLS flush";
    case HttpsFetchStageTlsRead: return "TLS read";
    case HttpsFetchStageHttpHeaders: return "HTTP headers";
    case HttpsFetchStageHttpStatus: return "HTTP status";
    case HttpsFetchStageHttpBody: return "HTTP body";
    default: return "unknown";
    }
}

namespace {
static void SetDiagnostic(HttpsFetchDiagnostic* diagnostic, HttpsFetchStage stage) {
    if (!diagnostic) return;
    diagnostic->stage = stage;
    diagnostic->status = 0;
    diagnostic->has_status = false;
}
static void SetDiagnosticStatus(HttpsFetchDiagnostic* diagnostic, HttpsFetchStage stage, int32_t status) {
    if (!diagnostic) return;
    diagnostic->stage = stage;
    diagnostic->status = status;
    diagnostic->has_status = true;
}
static const char kHost[] = "raw.githubusercontent.com";
static const char kPath[] = "/rghmodder1991/License360/main/iphonetether360.txt";
struct IoContext { B9cTcpStream* stream; const Deadline* deadline; };
static int IoRead(void* opaque, unsigned char* buf, size_t len) {
    IoContext* io = static_cast<IoContext*>(opaque);
    if (!io || !io->stream || !io->deadline || len > 0x7FFFFFFFu) return -1;
    return io->stream->Read(buf, static_cast<unsigned>(len), *io->deadline);
}
static int IoWrite(void* opaque, const unsigned char* buf, size_t len) {
    IoContext* io = static_cast<IoContext*>(opaque);
    if (!io || !io->stream || !io->deadline || len > 0x7FFFFFFFu) return -1;
    return io->stream->Write(buf, static_cast<unsigned>(len), *io->deadline);
}
static const char* FindHeaderEnd(const char* data, unsigned length) {
    if (!data) return 0;
    for (unsigned i=0;i+3u<length;++i)
        if (data[i]=='\r'&&data[i+1]=='\n'&&data[i+2]=='\r'&&data[i+3]=='\n') return data+i+4u;
    return 0;
}
static bool StartsNoCase(const char* p,const char* s) {
    while (*s) { char a=*p++,b=*s++; if(a>='A'&&a<='Z')a=(char)(a+32); if(b>='A'&&b<='Z')b=(char)(b+32); if(a!=b)return false; }
    return true;
}
static bool HeaderContains(const char* begin,const char* end,const char* needle) {
    if (!begin || !end || !needle) return false;
    const unsigned n = static_cast<unsigned>(strlen(needle));
    for (const char* p = begin; p + n <= end; ++p)
        if (StartsNoCase(p, needle)) return true;
    return false;
}
static int ContentLength(const char* begin,const char* end) {
    static const char k[]="content-length:"; const unsigned n=sizeof(k)-1u;
    for(const char* p=begin;p+n<=end;++p){if(!StartsNoCase(p,k))continue;p+=n;while(p<end&&(*p==' '||*p=='\t'))++p;int v=0,seen=0;while(p<end&&*p>='0'&&*p<='9'){seen=1;if(v>1000000)return -1;v=v*10+(*p++-'0');}return seen?v:-1;} return -1;
}
static int HexValue(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static bool DecodeChunked(const char* src,unsigned n,char* out,unsigned cap,unsigned* out_n){unsigned p=0,w=0;while(p<n){unsigned size=0,digits=0;while(p<n&&src[p]!='\r'){int h=HexValue(src[p]);if(h<0)return false;if(size>0x0FFFFFFFu)return false;size=(size<<4)|(unsigned)h;++digits;++p;}if(!digits||p+1>=n||src[p]!='\r'||src[p+1]!='\n')return false;p+=2;if(size==0){*out_n=w;return true;}if(p+size+2u>n||w+size>cap)return false;memcpy(out+w,src+p,size);w+=size;p+=size;if(src[p]!='\r'||src[p+1]!='\n')return false;p+=2;}return false;}
static void SetCertificateTime(br_x509_minimal_context* xc) {
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    int64_t ft=0; KeQuerySystemTime(&ft); if(ft<=116444736000000000LL)return;
    const uint64_t unix_seconds=(static_cast<uint64_t>(ft)-116444736000000000ULL)/10000000ULL;
    const uint32_t days=719528u+static_cast<uint32_t>(unix_seconds/86400ULL);
    const uint32_t seconds=static_cast<uint32_t>(unix_seconds%86400ULL);
    br_x509_minimal_set_time(xc,days,seconds);
#else
    (void)xc;
#endif
}
static bool SeedEngine(br_ssl_engine_context* eng) {
    unsigned char entropy[32];
#if defined(IT360_XBOX) && defined(IT360_OPENXECHAIN)
    XeCryptRandom(entropy, sizeof(entropy));
#else
    for(unsigned i=0;i<sizeof(entropy);++i) entropy[i]=(unsigned char)(0xA5u^(i*37u));
#endif
    br_ssl_engine_inject_entropy(eng,entropy,sizeof(entropy));
    it360_license::SecureZero(entropy,sizeof(entropy));
    return true;
}
}
HttpsFetchResult FetchFixedLicenceFile(B9cTcpStream& stream, char* body, unsigned cap,
                                       unsigned* body_len, const Deadline& deadline,
                                       HttpsFetchDiagnostic* diagnostic) {
    if (diagnostic) *diagnostic = HttpsFetchDiagnostic();
    if (!body || !cap || !body_len) {
        SetDiagnostic(diagnostic, HttpsFetchStageArguments);
        return HttpsFetchHttpFailed;
    }
    *body_len = 0;
    body[0] = 0;

    DWORD address = 0;
    if (!stream.ResolveHost(kHost, &address, deadline)) {
        SetDiagnostic(diagnostic, HttpsFetchStageDns);
        return deadline.Expired() ? HttpsFetchTimedOut : HttpsFetchDnsFailed;
    }
    if (!stream.Connect(address, 443u, deadline)) {
        SetDiagnostic(diagnostic, HttpsFetchStageConnect);
        return deadline.Expired() ? HttpsFetchTimedOut : HttpsFetchConnectFailed;
    }

    size_t anchor_count = 0;
    const br_x509_trust_anchor* anchors = SectigoTrustAnchors(&anchor_count);
    if (!anchors || !anchor_count) {
        SetDiagnostic(diagnostic, HttpsFetchStageTrustAnchors);
        stream.Close();
        return HttpsFetchTlsFailed;
    }

    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context ioc;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    br_ssl_client_init_full(&sc, &xc, anchors, anchor_count);
    SetCertificateTime(&xc);
    br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof(iobuf), 1);
    SeedEngine(&sc.eng);

    if (!br_ssl_client_reset(&sc, kHost, 0)) {
        SetDiagnosticStatus(diagnostic, HttpsFetchStageTlsReset,
                            static_cast<int32_t>(br_ssl_engine_last_error(&sc.eng)));
        stream.Close();
        return HttpsFetchTlsFailed;
    }

    IoContext io;
    io.stream = &stream;
    io.deadline = &deadline;
    br_sslio_init(&ioc, &sc.eng, IoRead, &io, IoWrite, &io);

    char request[320];
    const int rn = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: iPhoneTether360/1.0\r\nAccept: text/plain\r\nConnection: close\r\n\r\n",
        kPath, kHost);
    if (rn <= 0 || static_cast<unsigned>(rn) >= sizeof(request)) {
        SetDiagnostic(diagnostic, HttpsFetchStageRequestFormat);
        stream.Close();
        return HttpsFetchHttpFailed;
    }
    if (br_sslio_write_all(&ioc, request, static_cast<size_t>(rn)) < 0) {
        SetDiagnosticStatus(diagnostic, HttpsFetchStageTlsWrite,
                            static_cast<int32_t>(br_ssl_engine_last_error(&sc.eng)));
        stream.Close();
        return deadline.Expired() ? HttpsFetchTimedOut : HttpsFetchTlsFailed;
    }
    if (br_sslio_flush(&ioc) < 0) {
        SetDiagnosticStatus(diagnostic, HttpsFetchStageTlsFlush,
                            static_cast<int32_t>(br_ssl_engine_last_error(&sc.eng)));
        stream.Close();
        return deadline.Expired() ? HttpsFetchTimedOut : HttpsFetchTlsFailed;
    }

    char response[12288];
    unsigned used = 0;
    while (used < sizeof(response) && !deadline.Expired()) {
        const int r = br_sslio_read(&ioc, response + used, sizeof(response) - used);
        if (r < 0) {
            SetDiagnosticStatus(diagnostic, HttpsFetchStageTlsRead,
                                static_cast<int32_t>(br_ssl_engine_last_error(&sc.eng)));
            stream.Close();
            return deadline.Expired() ? HttpsFetchTimedOut : HttpsFetchTlsFailed;
        }
        if (r == 0) break;
        used += static_cast<unsigned>(r);
        const char* he = FindHeaderEnd(response, used);
        if (he) {
            const unsigned hoff = static_cast<unsigned>(he - response);
            const int cl = ContentLength(response, he);
            if (cl >= 0 && hoff + static_cast<unsigned>(cl) <= used) break;
        }
    }
    stream.Close();
    if (deadline.Expired()) {
        SetDiagnostic(diagnostic, HttpsFetchStageTlsRead);
        return HttpsFetchTimedOut;
    }

    const char* he = FindHeaderEnd(response, used);
    if (!he) {
        SetDiagnostic(diagnostic, HttpsFetchStageHttpHeaders);
        return HttpsFetchHttpFailed;
    }
    if (used < 12u || memcmp(response, "HTTP/1.", 7) != 0 ||
        response[9] < '0' || response[9] > '9' ||
        response[10] < '0' || response[10] > '9' ||
        response[11] < '0' || response[11] > '9') {
        SetDiagnostic(diagnostic, HttpsFetchStageHttpStatus);
        return HttpsFetchHttpFailed;
    }
    const int http_status = (response[9] - '0') * 100 +
                            (response[10] - '0') * 10 +
                            (response[11] - '0');
    if (http_status != 200) {
        SetDiagnosticStatus(diagnostic, HttpsFetchStageHttpStatus, http_status);
        return HttpsFetchHttpFailed;
    }

    const unsigned header_len = static_cast<unsigned>(he - response);
    const unsigned payload_len = used - header_len;
    if (HeaderContains(response, he, "transfer-encoding: chunked")) {
        unsigned out = 0;
        if (!DecodeChunked(he, payload_len, body, cap, &out)) {
            SetDiagnostic(diagnostic, HttpsFetchStageHttpBody);
            return payload_len >= cap ? HttpsFetchTooLarge : HttpsFetchHttpFailed;
        }
        *body_len = out;
        body[out < cap ? out : cap - 1u] = 0;
        return HttpsFetchOk;
    }
    const int cl = ContentLength(response, he);
    const unsigned need = cl >= 0 ? static_cast<unsigned>(cl) : payload_len;
    if (need > payload_len) {
        SetDiagnostic(diagnostic, HttpsFetchStageHttpBody);
        return HttpsFetchHttpFailed;
    }
    if (need > cap) {
        SetDiagnostic(diagnostic, HttpsFetchStageHttpBody);
        return HttpsFetchTooLarge;
    }
    memcpy(body, he, need);
    *body_len = need;
    if (need < cap) body[need] = 0;
    return HttpsFetchOk;
}
}
