/* Serial2 on its own: the linker takes this file only when a sketch names it. */
#include "HardwareSerial.h"

#if defined(CH32RV_SERIAL2_TX)
arduino::CH32RVHardwareSerial Serial2(2);
#endif
