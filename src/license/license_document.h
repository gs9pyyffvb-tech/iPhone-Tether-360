#pragma once
namespace it360_license_runtime {
enum LicenceDocumentVerdict {
    LicenceDocumentInvalid = 0,
    LicenceDocumentMatched = 1,
    LicenceDocumentFree = 2,
    LicenceDocumentNoMatch = 3
};
LicenceDocumentVerdict EvaluateLicenceDocument(const char* body, unsigned body_length,
                                                const char licence_id[65]);
}
