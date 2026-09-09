# OpenXeChain port audit

This checkpoint centralizes Xbox-toolchain interaction behind `src/platform/xbox_platform.*`, `build_openxechain.sh`, and the GitHub Actions workflow.

## Dependency replacements

| Former dependency pattern | Current OpenXeChain path |
| --- | --- |
| proprietary umbrella console header | `xecore/xboxkrnl.h` + `xecore/xam.h` |
| compiler-specific DLL startup symbol | OpenXeChain Newlib `_start` dispatching to `DllMain` |
| runtime thread convenience wrapper | `ExCreateThread` raw worker + explicit `ExTerminateThread` |
| millisecond sleep wrapper | `KeDelayExecutionThread` |
| runtime atomic convenience wrappers | compiler PowerPC atomic builtins |
| compiler-specific alignment/section directives | Clang `aligned` / `section` attributes |
| proprietary console import libraries | `xecorelib.a` |
| proprietary PE-to-XEX conversion stage | SynthXEX |
| local desktop console-toolchain requirement | GitHub Actions + pinned OpenXeChain buildscript |

## Runtime-sensitive checks

- Kernel build is read before the recovered 17559-only matcher is touched.
- The matcher prologue must exactly equal the known 17559 instructions before patching.
- Modified code is synchronized through Xenon D-cache/I-cache operations before execution resumes.
- Callback/function addresses that are stored in recovered 32-bit USB structures pass through an explicit 32-bit address helper.
- Recovered USB structure sizes are static-asserted in the real 32-bit OpenXeChain target build.
- OpenXeChain's Xbox CRT entry contract is matched by the project `DllMain` signature.
- Pair private keys remain host-side and never enter the Pair request.
- Host regressions retain Pair/ValidatePair, NCM, DHCP, ARP, DNS, TCP and HTTP packet-path coverage.
- The GitHub target build structurally verifies the generated Xbox PE and final XEX before artifact upload.

## Audit boundary

The source audit proves that obsolete toolchain dependencies are no longer required by the project tree and that the expected OpenXeChain API/build paths are present. A successful GitHub target build proves compile/link compatibility with the pinned OpenXeChain revision. Only the physical kernel-17559 console and iOS 26.1 test can prove the recovered USB ABI and live phone behavior at runtime.

## Runtime-safety decisions

- `XNotifyQueueUI` is not called by default in the OpenXeChain build. The diagnostics worker is a raw system thread, and calling XAM notifications from that context is known to require a separate user-thread bridge or system patch. Persistent file logging and `DbgPrint` remain enabled.
