/* Serial on its own: the linker takes this file only when a sketch names it.
 * It is row 0 of the enabled USARTs (ch32rv_instances.h). */
#include "HardwareSerial.h"

#if CH32RV_SERIAL_COUNT > 0
arduino::CH32RVHardwareSerial Serial(0);
#endif
