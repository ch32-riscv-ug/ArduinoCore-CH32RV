/* Wire1 on its own: the linker takes this file only when a sketch names it.
 * It is the second I2C that is on (ch32rv_instances.h), I2C2: its row of the
 * hardware driver's table, or with I2C2 set to Software, a SoftWire on its
 * default pins. */
#include "Wire.h"

#if CH32RV_WIRE_COUNT > 1
#if !CH32RV_WIRE1_SOFT
arduino::CH32RVTwoWire Wire1(CH32RV_WIRE1_ROW);
#elif defined(CH32RV_I2C2_SDA)
arduino::SoftWire Wire1(CH32RV_I2C2_SDA, CH32RV_I2C2_SCL);
#else
arduino::SoftWire Wire1(CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN);
#endif
#endif
