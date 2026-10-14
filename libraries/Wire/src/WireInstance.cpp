/* Wire on its own: the linker takes this file only when a sketch names it. The
 * bare name is the first bus and Wire1 the second, as on Due, Zero, STM32duino
 * and arduino-pico: a library asking for Wire1 means "the other one". */
#include "Wire.h"

#if defined(CH32RV_I2C1_SCL)
arduino::CH32RVTwoWire Wire(1);
#elif defined(CH32RV_I2C2_SCL)
arduino::CH32RVTwoWire Wire(2);      /* a part that bonds only the second I2C */
#endif
