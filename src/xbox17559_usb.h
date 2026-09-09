#pragma once
#include "platform/xbox_platform.h"

namespace iphone_probe {

#pragma pack(push, 1)
struct UsbDeviceDescriptor {
    BYTE bLength;
    BYTE bDescriptorType;
    WORD bcdUSB;
    BYTE bDeviceClass;
    BYTE bDeviceSubClass;
    BYTE bDeviceProtocol;
    BYTE bMaxPacketSize0;
    WORD idVendor;
    WORD idProduct;
    WORD bcdDevice;
    BYTE iManufacturer;
    BYTE iProduct;
    BYTE iSerialNumber;
    BYTE bNumConfigurations;
};

struct UsbConfigurationDescriptor {
    BYTE bLength;
    BYTE bDescriptorType;
    WORD wTotalLength;
    BYTE bNumInterfaces;
    BYTE bConfigurationValue;
    BYTE iConfiguration;
    BYTE bmAttributes;
    BYTE bMaxPower;
};

struct UsbInterfaceDescriptor {
    BYTE bLength;
    BYTE bDescriptorType;
    BYTE bInterfaceNumber;
    BYTE bAlternateSetting;
    BYTE bNumEndpoints;
    BYTE bInterfaceClass;
    BYTE bInterfaceSubClass;
    BYTE bInterfaceProtocol;
    BYTE iInterface;
};

struct UsbEndpointDescriptor {
    BYTE bLength;
    BYTE bDescriptorType;
    BYTE bEndpointAddress;
    BYTE bmAttributes;
    WORD wMaxPacketSize;
    BYTE bInterval;
};

struct UsbSetupPacket {
    BYTE bmRequestType;
    BYTE bRequest;
    WORD wValue;
    WORD wIndex;
    WORD wLength;
};
#pragma pack(pop)

struct DeviceHandle {
    void* driverExtension;
};

struct UsbTrb {
    DWORD endpoint;
    DWORD callback;
    DWORD savedEndpoint;
    BYTE padding[4];
    BYTE flags;
    BYTE controllerIndex;
    BYTE pad2;
    BYTE endpointIndex;
    void* buffer;
    DWORD length;
};

struct UsbControlTrb {
    UsbTrb trb;
    BYTE pad[4];
    UsbSetupPacket packet;
};

#if defined(IT360_OPENXECHAIN)
// These layouts mirror the 17559 USB structures the recovered driver path
// consumes. Fail the OpenXeChain build immediately if target ABI/layout ever
// changes instead of producing an XEX with silently wrong field offsets.
IT360_STATIC_ASSERT(usb_device_descriptor_18, sizeof(UsbDeviceDescriptor) == 18);
IT360_STATIC_ASSERT(usb_config_descriptor_9, sizeof(UsbConfigurationDescriptor) == 9);
IT360_STATIC_ASSERT(usb_interface_descriptor_9, sizeof(UsbInterfaceDescriptor) == 9);
IT360_STATIC_ASSERT(usb_endpoint_descriptor_7, sizeof(UsbEndpointDescriptor) == 7);
IT360_STATIC_ASSERT(usb_setup_packet_8, sizeof(UsbSetupPacket) == 8);
IT360_STATIC_ASSERT(device_handle_4, sizeof(DeviceHandle) == 4);
IT360_STATIC_ASSERT(usb_trb_28, sizeof(UsbTrb) == 28);
IT360_STATIC_ASSERT(usb_control_trb_40, sizeof(UsbControlTrb) == 40);
#endif

typedef int (*UsbAddDeviceFn)(DeviceHandle*);
typedef int (*UsbRemoveDeviceFn)(DeviceHandle*);
typedef int (*UsbNodeMatchFn)(UsbInterfaceDescriptor*, UsbDeviceDescriptor*, void*);
typedef int (*UsbNotifyFn)(void*);

struct UsbDriverDescriptor17559 {
    void* flink;
    void* blink;
    DWORD metadata;
    UsbAddDeviceFn addDevice;
    UsbRemoveDeviceFn removeDevice;
    UsbNodeMatchFn match;
    UsbNotifyFn notify;
};

typedef int (*UsbdAddDeviceCompleteFn)(DeviceHandle*, int);
typedef int (*UsbdGetDeviceSpeedFn)(DeviceHandle*);
typedef NTSTATUS (*UsbdOpenDefaultEndpointFn)(DeviceHandle*, DWORD*);
typedef NTSTATUS (*UsbdOpenEndpointFn)(DeviceHandle*, int, int, int, int, DWORD*);
typedef int (*UsbdQueueAsyncTransferFn)(DeviceHandle*, void*);
typedef NTSTATUS (*UsbdQueueCloseDefaultEndpointFn)(DeviceHandle*, void*);
typedef NTSTATUS (*UsbdQueueCloseEndpointFn)(DeviceHandle*, void*);
typedef NTSTATUS (*UsbdRemoveDeviceCompleteFn)(DeviceHandle*);
typedef UsbDeviceDescriptor* (*UsbdGetDeviceDescriptorFn)(DeviceHandle*);
typedef UsbInterfaceDescriptor* (*UsbdGetInterfaceDescriptorFn)(DeviceHandle*);
typedef UsbConfigurationDescriptor* (*UsbdGetConfigurationDescriptorFn)(DeviceHandle*);
typedef UsbEndpointDescriptor* (*UsbdGetEndpointDescriptorFn)(DeviceHandle*, int, int, int);

typedef UsbDriverDescriptor17559* (*DynamicUsbMatcher17559Fn)(
    UsbInterfaceDescriptor*, UsbDeviceDescriptor*);

inline WORD Swap16(WORD v) {
    return static_cast<WORD>((v >> 8) | (v << 8));
}

} // namespace iphone_probe
