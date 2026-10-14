/* Serial4 on its own: the linker takes this file only when a sketch names it. */
#include "HardwareSerial.h"

#if defined(CH32RV_SERIAL4_TX)
arduino::CH32RVHardwareSerial Serial4(4);
#endif
