/* Wire1 on its own: the linker takes this file only when a sketch names it. */
#include "Wire.h"

#if defined(CH32RV_I2C1_SCL) && defined(CH32RV_I2C2_SCL)
arduino::CH32RVTwoWire Wire1(2);
#endif
