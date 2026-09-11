# iPhoneTether360 1.0.0 hardware test — Xbox 360 kernel 17559 + iPhone

This tree is ready for its final OpenXeChain PowerPC build and then physical hardware validation. Do not configure it as a mandatory boot plugin until the first manual load is stable.

## 1. Build

Push the complete final tree to the repository and run the **Build iPhoneTether360 1.0.0** GitHub Actions workflow. Download artifact:

```text
iPhoneTether360-1.0.0-OpenXeChain
```

It must contain `Loader.xex`, `Core.xex`, `LicenseID.xex`, their SHA-256 files, `BUILD_OUTPUTS.txt`, and `VERSION.txt`.

## 2. Deploy

Put `Loader.xex`, `Core.xex`, and `LicenseID.xex` in the same application directory. For the first test, launch `Loader.xex` manually from Aurora. Loader should load resident `Core.xex` and return to Aurora. `launch.ini.example` is only an optional direct-Core autoload example for later use.

Keep the iPhone unlocked, enable cellular data and Personal Hotspot, and approve **Trust This Computer?** if iOS presents it.

## 3. Startup diagnostics

Normal startup history is app-local `Boot.log`; runtime diagnostics are app-local `Log.txt`. If Core freezes before its application directory is resolved, the emergency pre-CRT trace is synchronously written to:

```text
Hdd1:\iPhoneTether360.log
```

The successful handoff replays the early trace into `Boot.log`. There is no USB logging fallback.

`iPhoneTether360 Ready` means Core loaded successfully and is ready for the phone to be connected; it does not mean tethering is already active.

## 4. Pair and tether progression

Expected milestones include Pair/ValidatePair success, Apple tether interface discovery, tether MAC acquisition, NCM/legacy RX setup, `SET_INTERFACE(2,1)`, carrier ON, DHCP, gateway ARP, DNS/TCP Internet validation, licensing classification and native Xbox bridge activation.

Expected user-facing progression includes:

```text
iPhone Detected
Checking iPhone Data...
iPhone Data Working
License Has Been Found
iPhone to Xbox Complete
```

For a valid allow-list response where the console is not licensed, expect:

```text
No License found for this Xbox
```

If the licensing service itself is unavailable, the network gate deliberately fails open; an explicit valid unlicensed response does not.

## 5. Native Xbox network gate

`iPhone to Xbox Complete` is the final success condition. It is emitted only when Internet data has been proven, the licensing worker has completed, the licence gate permits native networking, and the B9D native bridge is active. At that point the test advances beyond the earlier standalone B9C proof and into the Xbox networking path.

## 6. Failure capture

After any failure, preserve the complete `Boot.log` and `Log.txt`. If the machine froze before normal-path handoff, also preserve `Hdd1:\iPhoneTether360.log`.

The Step-3 diagnostics retain the exact operation and hexadecimal return/status wherever the underlying Xbox API exposes one. Report the last successful `BOOT`, `ENTRY`, USB, Pair, network, licence or B9D line rather than paraphrasing it.

Useful boundaries are: no emergency entry line = XEX load/entry problem; emergency entry but no normal handoff = CRT/path/startup problem; no USB match = matcher/USB path; no Pair/ValidatePair = lockdownd/trust/crypto; no tether MAC/NCM/interface = Apple USB control path; carrier ON but no DHCP = Ethernet/NCM; DHCP but no Internet proof = IP/DNS/TCP path; Internet proof but no licence decision = TLS/licensing; licence allows but no completion = native B9D bridge.

## 7. Reconnect test

After a successful first connection, unplug the iPhone, confirm `iPhone Disconnected`, reconnect it, confirm persisted `ValidatePair` reuse, and verify the data/licence/native bridge progression returns through `iPhone to Xbox Complete` without restarting the console.
