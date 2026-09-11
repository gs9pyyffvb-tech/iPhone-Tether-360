#include "net_stack.h"
#include <string.h>
namespace it360_net {
bool StandaloneNetStack::CopyGatewayMac(BYTE out[6]) const {
    if (!out || !gatewayMacValid_) return false;
    memcpy(out, gatewayMac_, 6);
    return true;
}
}
