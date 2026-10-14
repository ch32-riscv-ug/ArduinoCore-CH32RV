/* Serial1 on its own: the linker takes this file only when a sketch names it. */
#include "HardwareSerial.h"

#if defined(CH32RV_SERIAL1_TX)
arduino::CH32RVHardwareSerial Serial1(1);
#endif
