# Batch 9C OpenXeChain validation

## Completed host/source validation

Run:

```bash
bash tools/run_host_validation.sh
```

The suite currently validates:

- Apple limited-NCM parser;
- multi-datagram NCM RX;
- malformed NCM bounds rejection;
- Apple `00 01` control-frame drop;
- legacy aligned Ethernet RX;
- standalone simulated `DHCP -> ARP -> DNS -> TCP -> HTTP` path;
- DHCP retry and lease renewal;
- retained Batch 9B Pair-session regression;
- retained Batch 9B Pair protocol + persistence regression;
- embedded Root/Host identity and XeCrypt private-blob mathematical checks;
- runtime DeviceCertificate construction/signature checks;
- strict C++98 host syntax for all Xbox source units;
- OpenXeChain migration/reference audit;
- OpenXeChain-specific source/build integration checks.

## Toolchain migration checks

The migration audit fails if source/build/deployment files reintroduce legacy-only dependencies such as the old umbrella header, old image-conversion program, old import libraries, old DLL startup symbol, legacy thread wrappers or MSVC-only allocation/section directives.

OpenXeChain-specific behavior is centralized in `src/platform/xbox_platform.*`. The Xbox build uses `xecorelib` headers/import stubs and OpenXeChain Newlib's Xbox CRT. The final GitHub build additionally verifies the generated Xbox PE and SynthXEX output before upload.

## Runtime hardening retained

- USB callbacks do not enter DHCP/ARP/DNS/TCP state directly; RX frames are handed to a bounded queue.
- One worker owns the standalone network state.
- TX completion callbacks only clear transfer state.
- generation checks prevent stale detach/replug workers from touching a new device.
- the kernel patch refuses to install unless the recovered 17559 matcher prologue exactly matches.
- instruction/data caches are explicitly synchronized after patching.
- callback addresses are explicitly narrowed to the Xbox 360 32-bit ABI.
- recovered USB structure sizes are target-build static assertions.

## Physical hardware gate still required

The source and host regressions cannot prove:

- the generated XEX is accepted by the user's exact DashLaunch/kernel environment until it is loaded on hardware;
- the recovered 17559 internal matcher/helper addresses on the physical console;
- real iPhone FF/FE/02 and FF/FD/01 descriptors/endpoints;
- real Xbox USB completion timing/length behavior;
- live Xenon XeCrypt execution;
- the iOS 26.1 Trust/Pair response timing;
- Personal Hotspot carrier responses;
- real DHCP/NCM traffic;
- the final HTTP success marker over cellular Internet.

`HARDWARE_TEST.md` defines the exact observable checkpoints for that gate.
