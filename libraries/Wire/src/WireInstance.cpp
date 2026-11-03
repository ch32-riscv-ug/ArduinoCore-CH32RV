/* Wire on its own: the linker takes this file only when a sketch names it.
 * It is the first I2C that is on (ch32rv_instances.h): row 0 of the hardware
 * driver's table, or with that I2C set to Software, a SoftWire on its default
 * pins. */
#include "Wire.h"

#if CH32RV_WIRE_COUNT > 0
#if !CH32RV_WIRE0_SOFT
arduino::CH32RVTwoWire Wire(0);
#elif CH32RV_WIRE0_INSTANCE == 1 && defined(CH32RV_I2C1_SDA)
arduino::SoftWire Wire(CH32RV_I2C1_SDA, CH32RV_I2C1_SCL);
#elif CH32RV_WIRE0_INSTANCE == 2 && defined(CH32RV_I2C2_SDA)
arduino::SoftWire Wire(CH32RV_I2C2_SDA, CH32RV_I2C2_SCL);
#else
/* No default route on this part: begin(sda, scl) names the pins. */
arduino::SoftWire Wire(CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN);
#endif
#endif
