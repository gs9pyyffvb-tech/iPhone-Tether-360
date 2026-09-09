# Batch 9C source / protocol basis

## Apple USB Ethernet (`ipheth`)

Primary reference:

`https://github.com/torvalds/linux/blob/master/drivers/net/usb/ipheth.c`

The current Linux driver establishes the relevant behavior used by this checkpoint:

- Apple vendor ID `0x05AC`.
- USB interface class/subclass/protocol `255/253/1` (`FF/FD/01`).
- tether interface number 2, alternate setting 1.
- `GET_MACADDR` request `0x00`, request type `0xC0`, value 0, index 2, 0x40-byte control buffer.
- `ENABLE_NCM` request `0x04`, request type `0x41`, value 0, index 2, no payload.
- `CARRIER_CHECK` request `0x45`, request type `0xC0`, value 0, index 2; carrier marker `0x04`.
- 64 KiB NCM RX buffer.
- iOS NCM RX uses a 12-byte NTH16 followed directly by a 96-byte NDP16 region with up to 22 DPEs including the null trailer.
- in NCM mode only iPhone-to-host RX traffic is NCM-framed; host-to-iPhone TX traffic remains raw Ethernet.
- a zero-payload Bulk IN completion is harmless.
- the initial `00 01` four-byte Bulk IN control frame can be discarded.
- legacy RX has two alignment bytes before the Ethernet frame.
- the iPhone needs to have been paired before the Ethernet driver will respond.

## usbmux separation

Primary reference:

`https://github.com/libimobiledevice/usbmuxd`

usbmux is used for multiplexed device services such as lockdownd. USB tethering uses a separate USB network interface, which is why Batch 9B and Batch 9C claim different Apple USB interfaces.

## Xbox 360 kernel 17559 attachment basis

The Xbox-specific portion is based on the retained reconstructed-kernel work from earlier project batches:

- dynamic USB matcher: `0x800D6178`;
- generic AddDevice router: `0x800D6F20`;
- raw configuration-descriptor helper: `0x800D83A0`;
- interface-descriptor helper: `0x800D8500`;
- exported USB functions 740, 742, 746-751 and 759.

These addresses/ABIs are specifically for the project's kernel-17559 target and remain a physical-hardware validation item.

## Pair / Trust basis retained from Batch 9B

The Batch 9B lockdownd implementation follows current libimobiledevice Pair/ValidatePair behavior where applicable:

- `GetValue` values are read from the generic `Value` plist key;
- Root/Host private keys stay on the host and are not sent in Pair;
- the device certificate is derived from the iPhone DevicePublicKey and signed by the generated Root identity;
- Trust-dialog-pending is retried without regenerating the host identity;
- `EscrowBag` and WiFiAddress are retained in the local pair record;
- Pair/ValidatePair responses are tied to the matching `Request`.


## OpenXeChain target basis

The port is built against the current OpenXeChain aggregate toolchain revision pinned by the GitHub Actions workflow. The relevant components are:

- OpenXeChain LLVM/Clang target `ppc32-xbox360`;
- OpenXeChain Newlib Xbox 360 CRT/syscalls;
- `xecorelib` kernel/XAM import stubs and headers;
- SynthXEX for PE-to-XEX2 packaging.

The port specifically relies on `xecorelib` coverage for `DbgPrint`, `ExCreateThread`, `ExTerminateThread`, `KeDelayExecutionThread`, `XexGetModuleHandle`, `XexGetProcedureAddress`, `NtClose`, the XeCrypt ordinals used by Batch 9B, and the XAM file I/O imports. The USB driver functions used by the project are resolved by ordinal at runtime so that their recovered prototypes remain project-local and tied to kernel 17559.

OpenXeChain Newlib's Xbox CRT provides `_start` and dispatches to `DllMain` when no `main` symbol exists, which matches the plugin/system-DLL packaging used here.
