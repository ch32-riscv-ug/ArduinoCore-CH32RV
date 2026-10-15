/* Serial6 on its own: the linker takes this file only when a sketch names it.
 * It is row 6 of the enabled USARTs (ch32rv_instances.h). */
#include "HardwareSerial.h"

#if CH32RV_SERIAL_COUNT > 6
arduino::CH32RVHardwareSerial Serial6(6);
#endif
