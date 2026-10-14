/* Serial3 on its own: the linker takes this file only when a sketch names it. */
#include "HardwareSerial.h"

#if defined(CH32RV_SERIAL3_TX)
arduino::CH32RVHardwareSerial Serial3(3);
#endif
