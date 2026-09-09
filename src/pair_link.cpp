#include "platform/xbox_platform.h"
#include "pair_link.h"
namespace it360_link {
static volatile LONG gReady = 0;
void SetPairReady(bool ready) {
#ifdef IT360_XBOX
    it360_platform::AtomicExchange(&gReady, ready ? 1 : 0);
#else
    gReady = ready ? 1 : 0;
#endif
}
bool IsPairReady() {
#ifdef IT360_XBOX
    return it360_platform::AtomicCompareExchange(&gReady, 0, 0) != 0;
#else
    return gReady != 0;
#endif
}
}
