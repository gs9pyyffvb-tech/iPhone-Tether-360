# Batch 9C OpenXeChain hardware test — Xbox 360 kernel 17559 + iPhone iOS 26.1

This package is source-complete through Batch 9C. Hardware confirmation requires a successful OpenXeChain build and a physical console/iPhone run.

## 1. Produce the XEX in GitHub

Push the complete source tree to a GitHub repository, then run:

```text
Actions -> Build iPhoneTether360 XEX -> Run workflow
```

Download the completed Actions artifact:

```text
iPhoneTether360-B9C-OpenXeChain
```

Use this file from the artifact:

```text
iPhoneTether360-B9C-OXC.xex
```

The workflow performs the source migration audit, host regressions, OpenXeChain target compilation, SynthXEX conversion, structural PE/XEX verification and SHA-256 generation before publishing the artifact.

## 2. Deploy for the first test

Keep this as a manual/development test rather than an automatic boot dependency until the hardware path is stable. The XEX itself is a system-DLL/plugin module; load it with your chosen plugin-loader test method. `launch.ini.example` shows the filename/path for later automatic DashLaunch use, but automatic boot loading is not required for the first development run.

Keep the iPhone unlocked. Enable cellular data and Personal Hotspot. If iOS presents **Trust This Computer?**, approve it and enter the phone passcode when requested.

## 3. Pair / ValidatePair gate

First pairing should eventually log:

```text
[iPhoneTether360:B9C] SUCCESS iPhone paired/trusted; 9B milestone reached
```

A previously paired iPhone should instead reach:

```text
[iPhoneTether360:B9C] SUCCESS persisted pairing validated/reused
```

## 4. Tether-interface bring-up

Expected progression:

```text
[iPhoneTether360:B9C] MATCH Apple tether interface FF/FD/01
[iPhoneTether360:B9C] MATCH tether VID=05ac PID=.... alt1 IN=../.. OUT=../..
[iPhoneTether360:B9C] pairing ready; starting ipheth control setup
[iPhoneTether360:B9C] tether MAC=xx:xx:xx:xx:xx:xx
[iPhoneTether360:B9C] Apple NCM RX mode enabled
[iPhoneTether360:B9C] SET_INTERFACE(2,1) complete
[iPhoneTether360:B9C] ipheth endpoints ready IN=../.. OUT=../.. NCM=1
[iPhoneTether360:B9C] Personal Hotspot carrier=ON
```

If `ENABLE_NCM` is rejected, the code intentionally falls back to legacy RX mode. That is not by itself a failure.

## 5. Standalone IP bring-up

Expected progression:

```text
[iPhoneTether360:B9C] DHCP Discover sent xid=........
[iPhoneTether360:B9C] DHCP Offer ...
[iPhoneTether360:B9C] DHCP ACK IP=... gateway=... DNS=... lease=...s
[iPhoneTether360:B9C] ARP gateway resolved ...
```

ICMP is diagnostic only; DNS/TCP validation does not require the gateway to answer ping.

## 6. Internet validation

Final expected progression:

```text
[iPhoneTether360:B9C] DNS example.com = ...
[iPhoneTether360:B9C] TCP/80 handshake complete
[iPhoneTether360:B9C] SUCCESS standalone Internet HTTP response received through iPhone
```

That final line proves:

```text
Xbox plugin -> Apple USB Ethernet -> iPhone Personal Hotspot -> Internet -> iPhone -> Xbox plugin
```

It does not yet make Aurora or games use the iPhone. That is Batch 9D.

## 7. Failure capture

Copy the complete persistent log after a failed run:

```text
Hdd1:\iPhoneTether360.log
```

If absent, check `Usb0:` through `Usb3:` for the same filename.

Useful failure boundaries:

- no plugin-start line: module packaging/loading problem;
- unsupported-kernel line: build is intentionally refusing to patch a non-17559 kernel;
- no FF/FE/02 match: usbmux-interface matching/USB hook issue;
- no Pair/ValidatePair success: lockdownd/crypto/trust issue;
- no FF/FD/01 match: tether-interface discovery problem;
- no tether MAC: Apple control-transfer problem or Pair not accepted;
- SET_INTERFACE/open-endpoint failure: recovered Xbox USB ABI/alternate-setting problem;
- carrier OFF: Personal Hotspot/iPhone state problem;
- carrier ON but no DHCP Offer: Ethernet TX/RX/NCM problem;
- DHCP succeeds but ARP fails: Ethernet receive/routing issue;
- ARP succeeds but DNS fails: IPv4/UDP/DNS issue;
- DNS succeeds but TCP fails: TCP/checksum/remote-path issue;
- TCP succeeds but no HTTP success: receive sequencing/content issue.

If the console freezes or reboots, preserve the tail of `iPhoneTether360.log` and the last `DbgPrint` line you can capture. OpenXeChain builds deliberately disable XAM toast notifications by default until a safe user-thread notification bridge is added.

## 8. Reconnect smoke test

After a successful run:

1. unplug the iPhone;
2. wait several seconds;
3. reconnect it;
4. confirm `ValidatePair` reuse rather than a fresh identity;
5. confirm carrier, DHCP and Internet validation return.
