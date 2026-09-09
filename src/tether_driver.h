#pragma once
#include "xbox17559_usb.h"
namespace it360_tether {
bool InitializeDriver();
bool IsTarget(iphone_probe::UsbInterfaceDescriptor* iface,
              iphone_probe::UsbDeviceDescriptor* dev);
iphone_probe::UsbDriverDescriptor17559* Driver();
}
