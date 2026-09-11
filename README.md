# iPhoneTether360 1.0.0 — OpenXeChain

iPhoneTether360 is an Xbox 360 RGH/JTAG homebrew package that connects an iPhone USB Personal Hotspot directly to the console. The target is kernel/dashboard **17559** and the build is **OpenXeChain only**; no proprietary Microsoft SDK headers or libraries are required.

## Final package

The application is split into three XEX files:

- `Loader.xex` — normal Aurora-launched title. It resolves its installation directory, opens `Boot.log`, refuses duplicate Core loads, loads `Core.xex` as the resident system DLL, then returns.
- `Core.xex` — resident sysdll containing Pair/Trust, Apple USB tethering, DHCP/ARP/DNS/TCP diagnostics, the native Xbox network bridge, licensing, notifications and persistent diagnostics.
- `LicenseID.xex` — one-time title utility that derives the app-specific SHA-256 licence ID from the console CPU key and writes `LicenseID.txt` beside the application.

Runtime files are created beside the XEX files where applicable: `Boot.log`, `Log.txt`, and `LicenseID.txt`. Before Core can resolve its application directory, pre-CRT checkpoints are synchronously written to `Hdd1:\iPhoneTether360.log`; once the normal path is available those checkpoints are replayed into app-local `Boot.log` and normal logging takes over.

## Build chain and pinned dependencies

GitHub Actions pins OpenXeChain aggregate buildscript revision `eed1fa65bf9577fd31625764b320a90182ea9ade`. That revision was audited against its pinned components, including:

- `xecorelib` `c65d67e071357acade681f04c46ae9719797f239`
- OpenXeChain Newlib `f929633c27099d404e9cb5b2739a9c7f9b6afccc`
- SynthXEX `48d1453a55468aa2f8a211db0b20edd594ef5be3`
- LLVM `890b83f6c8259a8899e182a5f7d9cf39c64131cc`

The Core TLS implementation additionally pins XboxTLS/BearSSL source revision `dc0cc21cbf6ae9e75f1974833742f80b45519326`. The build disables BearSSL Unix-time fallback and supplies certificate time from the Xbox kernel path instead.

The build sequence is:

```text
OpenXeChain Clang --target=ppc32-xbox360
  -> Loader.exe       -> SynthXEX title  -> Loader.xex
  -> Core.exe /dll    -> SynthXEX sysdll -> Core.xex
  -> LicenseID.exe    -> SynthXEX title  -> LicenseID.xex
```

All three intermediate PE images and all three XEX outputs are structurally verified. `Core.xex` is linked at base `0x91DE0000`; Loader requests Core with system/resident module flags `0x0000000A`.

## GitHub Actions

Run **Build iPhoneTether360 1.0.0**. The final artifact is named `iPhoneTether360-1.0.0-OpenXeChain` and contains:

```text
Loader.xex
Loader.sha256
Core.xex
Core.sha256
LicenseID.xex
LicenseID.sha256
BUILD_OUTPUTS.txt
VERSION.txt
```

The workflow runs the source/build-manifest, Step-4, migration and host regression audits before entering the expensive OpenXeChain build. A missing source/header or stale deployment artifact therefore fails early.

For a local build with an existing OpenXeChain sysroot:

```bash
OPENXECHAIN_PREFIX=/path/to/openxechain/sysroot bash ./build_openxechain.sh
```

## Core data path

The retained Pair/Trust and tether sequence is:

```text
Apple USB device
  -> usbmux FF/FE/02
     -> Pair / ValidatePair / persistent pair record
  -> tether FF/FD/01 interface 2
     -> GET_MACADDR
     -> ENABLE_NCM
     -> SET_INTERFACE(2,1)
     -> Bulk IN / Bulk OUT
     -> carrier state
     -> DHCP / ARP / DNS / TCP diagnostic path
     -> licensing decision (fail-open only when service is unavailable)
     -> native Xbox Ethernet bridge
     -> iPhone to Xbox Complete
```

The native bridge is implemented and remains disabled until the iPhone transport is configured and the licence gate permits it. A valid licence-list response that does not contain the console remains a deny; an unavailable/invalid licensing service does not strand the user's networking path.

## Kernel and USB safety

The project fails closed when the kernel build is not **17559** before installing the recovered USB matcher hook. The two unexported USB descriptor helpers (`0x800D83A0` and `0x800D8500`) are therefore intentionally limited to this target and contained within the USB driver implementation. Recovered TRB/control-TRB layouts have compile-time size assertions, and hook writes explicitly synchronize Xenon data/instruction caches.

Critical imported ordinals used by the current implementation were checked against pinned `xecorelib`, including module resolution/loading, native file I/O, thread functions, `XeCryptRandom`, `KeQuerySystemTime`, USB exports, `ExExpansionCall`, XAM Ethernet interception, and `XNotifyQueueUI` ordinal 656.

## Notifications

Core USB/network workers do not call `XNotifyQueueUI` directly. Runtime notifications are first queued by the Core diagnostic worker. If the eventual caller is already `PROC_USER`, the XAM UI call can run directly; otherwise the notification layer marshals the toast through XAM `CreateThread` ordinal 1084 so the UI call executes in user/XAPI thread context.

User-facing status messages include phone detection/data/reconnect/disconnect progress. `iPhoneTether360 Ready` means Core has loaded and is ready for the iPhone to be connected. The final successful native-path notification is `iPhone to Xbox Complete`.

## Licensing

`LicenseID.xex` reads the CPU key through the XeUnshackle-compatible HVPP expansion, derives the application-specific SHA-256 ID, writes only the derived ID, and wipes raw CPU-key/hash material. Core fetches the fixed allow-list over TLS and never writes the licensing host/path or CPU key into its runtime logs.

Required notifications are exactly:

```text
License Has Been Found
No License found for this Xbox
```

## Validation

Run:

```bash
bash tools/run_host_validation.sh
```

The host suite covers Pair/Trust, NCM, standalone network behavior, B9D frame translation/native bridge tests, recursive C++98 syntax checks for project translation units that do not require the external BearSSL checkout, and all source/static audits.

A clean host/static audit is a **pre-build gate**, not hardware proof. The final PowerPC OpenXeChain build must still succeed, its three generated XEX files must pass structural verification, and then the package must be tested on the target 17559 console/iPhone hardware.
 or ValidatePair success.

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
