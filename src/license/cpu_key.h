#pragma once
#include "platform/xbox_platform.h"
namespace it360_license {
enum CpuKeyFailure {
    CpuKeyFailureNone = 0,
    CpuKeyFailureInvalidArgument,
    CpuKeyFailureResolveExpansionCall,
    CpuKeyFailureInvalidFuseRead
};
struct CpuKeyDiagnostic {
    CpuKeyFailure failure;
    it360_platform::ResolveStatus resolve;
    CpuKeyDiagnostic() : failure(CpuKeyFailureNone), resolve() {}
};
const char* CpuKeyFailureName(CpuKeyFailure failure);
bool ReadCpuKey(BYTE out_key[16], CpuKeyDiagnostic* diagnostic = 0);
void SecureZero(void* data, size_t bytes);
}
