/* Serial1 on its own: the linker takes this file only when a sketch names it.
 * It is row 1 of the enabled USARTs (ch32rv_instances.h). */
#include "HardwareSerial.h"

#if CH32RV_SERIAL_COUNT > 1
arduino::CH32RVHardwareSerial Serial1(1);
#endif
