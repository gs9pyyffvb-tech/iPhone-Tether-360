# iPhoneTether360 1.0.0 target layout

## Loader.xex
Normal Aurora-launched title. Resolves its installation directory, appends to `Boot.log`, blocks duplicate Core loads, loads resident `Core.xex`, and returns to Aurora.

## Core.xex
Resident system DLL containing Pair/Trust, iPhone USB tethering, standalone network validation, the B9D native Xbox network bridge, notifications/runtime diagnostics, and the licence gate. Xbox network bridge (B9D) is implemented. Native registration/bridging is exposed only when the iPhone transport is configured and licensing allows networking or explicitly fails open because the service is unavailable.

## LicenseID.xex
One-time title utility. Reads the CPU key through the XeUnshackle-compatible HVPP expansion, derives the app-specific SHA-256 licence ID, writes `LicenseID.txt`, displays the ID, and wipes raw CPU-key/hash material.

## Runtime files
Generated beside the XEX files:
- `Boot.log`
- `Log.txt`
- `LicenseID.txt`

Before Core resolves its application directory, startup checkpoints are synchronously appended and flushed to `Hdd1:\iPhoneTether360.log` through native xboxkrnl file APIs. After path resolution, the buffered pre-CRT trace is replayed into app-local `Boot.log`, the normal logger takes ownership, and the emergency file is closed.

## Final audit state
Step 1 made the source tree self-contained. Step 2 hardened pre-CRT logging. Step 3 propagated exact status/error diagnostics. **Step 4 is the final static pre-build audit:** source/build-manifest closure, stale-verifier cleanup, pinned OpenXeChain/xecorelib API and ordinal checks, Loader/Core/LicenseID role checks, notification thread-context safety, all-three-XEX structural verification wiring, and final artifact packaging checks.

Passing Step 4 means the tree is ready for the final OpenXeChain PowerPC build. It does not replace the final build or 17559 hardware test.
