/* Wire1 on its own: the linker takes this file only when a sketch names it.
 * It is row 1 of the enabled I2Cs (ch32rv_instances.h). */
#include "Wire.h"

#if CH32RV_WIRE_COUNT > 1
arduino::CH32RVTwoWire Wire1(1);
#endif
