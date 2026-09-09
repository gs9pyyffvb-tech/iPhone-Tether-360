# iPhoneTether360 — Batch 9C OpenXeChain port

This tree contains the complete Batch 9B Pair/Trust implementation plus Batch 9C standalone iPhone USB Personal Hotspot networking, ported to the **OpenXeChain** Xbox 360 toolchain.

The current milestone is deliberately before Batch 9D. Batch 9C is intended to prove that the plugin itself can move Internet traffic through the iPhone. It does **not** yet register the iPhone link as an Xbox system NIC, so Aurora/games/XNet will not automatically use it until 9D is implemented.

## Toolchain

The target build uses:

```text
OpenXeChain LLVM/Clang (ppc32-xbox360)
        -> Xbox 360 PE/DLL
        -> SynthXEX --type sysdll
        -> iPhoneTether360-B9C-OXC.xex
```

The project uses OpenXeChain `xecorelib` for Xbox kernel/XAM imports and OpenXeChain Newlib for the Xbox CRT. There is no dependency on legacy proprietary compiler headers, libraries, compiler startup symbols or image-conversion tools.

The GitHub Actions workflow pins the aggregate OpenXeChain buildscript revision recorded in `.github/workflows/build-xex.yml`, builds the toolchain when it is not already cached, compiles the project, structurally verifies both the intermediate Xbox PE and final XEX, and publishes the XEX as an Actions artifact.

## Build with GitHub Actions

1. Put this complete source tree in a GitHub repository.
2. Open **Actions**.
3. Select **Build iPhoneTether360 XEX**.
4. Choose **Run workflow**.
5. When the job completes, download the artifact named:

```text
iPhoneTether360-B9C-OpenXeChain
```

It contains:

```text
iPhoneTether360-B9C-OXC.xex
iPhoneTether360-B9C-OXC.sha256
```

The workflow also runs the host packet/pairing regressions and the migration audit before the Xbox-target build.

## Local OpenXeChain build

If you already have an OpenXeChain sysroot installed, set `OPENXECHAIN_PREFIX` to it and run:

```bash
OPENXECHAIN_PREFIX=/path/to/openxechain/sysroot bash ./build_openxechain.sh
```

Expected output:

```text
build/iPhoneTether360-B9C-OXC.xex
```

## OpenXeChain compatibility layer

Xbox-specific toolchain/runtime interaction is centralized in:

```text
src/platform/xbox_platform.h
src/platform/xbox_platform.cpp
```

That layer provides the project with:

- kernel/XAM module and ordinal resolution through `XexGetModuleHandle` / `XexGetProcedureAddress`;
- raw detached worker threads through `ExCreateThread` with explicit `ExTerminateThread` completion;
- millisecond waits through `KeDelayExecutionThread`;
- 32-bit atomics via compiler PowerPC atomics;
- Xenon data/instruction-cache synchronization after the kernel hook is written;
- persistent file I/O through the XAM imports exposed by `xecorelib`;
- kernel build detection through `XboxKrnlVersion`.

The project fails closed if the console kernel build is not **17559** before touching the recovered 17559-only matcher address.

## End-to-end 9C flow

```text
Apple USB device
  |-- FF/FE/02 usbmux interface
  |     `-- Batch 9B: lockdownd Pair / ValidatePair
  |
  `-- FF/FD/01 interface 2
        `-- wait until Batch 9B reports pairing ready
            -> GET_MACADDR (0xC0 / 0x00, index 2)
            -> ENABLE_NCM (0x41 / 0x04, index 2)
            -> SET_INTERFACE(2, 1)
            -> open alternate-setting-1 Bulk IN / Bulk OUT
            -> CARRIER_CHECK (0xC0 / 0x45, index 2)
            -> raw Ethernet TX
            -> Apple limited-NCM Ethernet RX
            -> DHCP
            -> ARP gateway resolution
            -> ICMP diagnostic ping
            -> DNS A lookup for example.com
            -> TCP port 80 handshake
            -> HTTP/1.0 request
            -> standalone Internet-path success
```

## USB tether driver

`src/tether_driver.cpp` implements the Xbox-facing Apple USB Ethernet driver.

- Exact Apple match: VID `0x05AC`, interface 2, alternate 0, class/subclass/protocol `FF/FD/01`.
- Manually parses the raw configuration descriptor for interface 2 alternate setting 1 and its Bulk IN/OUT endpoints.
- Reuses the recovered kernel-17559 USB attachment route from earlier batches.
- Does not issue another device-level `SET_CONFIGURATION`; the tether interface waits until the usbmux/Pair path is ready, then changes only interface 2 to alternate setting 1.
- Reads the six-byte tether MAC address.
- Attempts Apple's NCM RX mode and falls back to the legacy two-byte-aligned receive format if NCM enable is rejected.
- Polls Personal Hotspot carrier state approximately once per second.
- Sends host-to-iPhone Ethernet frames raw, without NCM encapsulation.
- Continuously receives iPhone-to-host traffic using a 64 KiB NCM receive buffer.
- USB RX completions only decode/copy frames into a bounded hand-off queue. A single worker owns DHCP/ARP/DNS/TCP state.
- TX completion callbacks only clear transfer state; queue ownership remains on the worker thread.
- Detach/replug uses a generation guard and worker restart handoff.

## Apple NCM / legacy receive parser

`src/ipheth_ncm.cpp` handles:

- NTH16 `NCMH` header;
- NDP16 `NCM0` no-CRC table;
- the 108-byte iOS NCM header floor;
- multiple Ethernet datagrams in one USB transfer;
- strict index/length bounds checking;
- the initial `00 01` four-byte control frame;
- zero-payload completions;
- the legacy two-byte alignment prefix fallback.

## Standalone network validation stack

`src/net_stack.cpp` is deliberately a small diagnostic stack, not a replacement for Xbox networking. It implements:

- Ethernet II dispatch;
- ARP request/reply and gateway-MAC cache;
- DHCP Discover / Offer / Request / ACK;
- DHCP retry, T1 renewal and lease-expiry restart;
- IPv4 checksums and fragmented-packet rejection;
- ICMP echo diagnostics;
- UDP;
- DNS A lookup;
- minimal TCP;
- an HTTP/1.0 GET to `example.com` on port 80.

The final 9C hardware-success marker is:

```text
[iPhoneTether360:B9C] SUCCESS standalone Internet HTTP response received through iPhone
```

## Pairing dependency

Batch 9B remains in this tree and provides:

- usbmux v2 and lockdownd transport;
- fresh Pair/Trust;
- DeviceCertificate generation;
- persistent pair records;
- `ValidatePair` reconnect;
- Trust-dialog-pending retry;
- persistent diagnostics.

The tether driver does not begin Apple Ethernet setup until Batch 9B reports Pair or ValidatePair success.

## Diagnostics

Detailed output is appended to the first writable path:

1. `Hdd1:\iPhoneTether360.log`
2. `Usb0:\iPhoneTether360.log`
3. `Usb1:\iPhoneTether360.log`
4. `Usb2:\iPhoneTether360.log`
5. `Usb3:\iPhoneTether360.log`

`DbgPrint` output is retained as well. For the OpenXeChain build, XAM toast notifications are disabled by default because the diagnostic worker is a raw system thread and `XNotifyQueueUI` is not safe from system threads without an additional dashboard/RPC patch. The persistent log is the authoritative failure record. A future user-thread notification bridge can re-enable toasts without touching the USB/networking core.

## Validation

Run:

```bash
bash tools/run_host_validation.sh
```

The validation suite covers the portable Pair/Trust and packet code, source-level OpenXeChain compatibility, old-toolchain-reference detection, the recovered USB ABI layout assumptions, and the GitHub build configuration.

The actual console/phone gate is still required because a host test cannot prove kernel patch addresses, live USB transfer timing, XeCrypt behavior on Xenon, or iOS 26.1's physical Pair/Personal-Hotspot responses.

## What remains after 9C

Batch 9D must expose the validated Ethernet path to the stock Xbox networking stack so normal XNet/Winsock/XHTTP/Aurora traffic can use it. The kernel `NicRegisterDevice` path remains the leading integration direction.
