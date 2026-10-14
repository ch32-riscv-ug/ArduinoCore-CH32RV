/* Wire on its own: the linker takes this file only when a sketch names it.
 * It is row 0 of the enabled I2Cs (ch32rv_instances.h). */
#include "Wire.h"

#if CH32RV_WIRE_COUNT > 0
arduino::CH32RVTwoWire Wire(0);
#endif
