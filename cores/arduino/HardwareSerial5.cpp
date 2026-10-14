/* Serial5 on its own: the linker takes this file only when a sketch names it. */
#include "HardwareSerial.h"

#if defined(CH32RV_SERIAL5_TX)
arduino::CH32RVHardwareSerial Serial5(5);
#endif
