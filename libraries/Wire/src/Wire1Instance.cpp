/* Wire1 on its own: the linker takes this file only when a sketch names it.
 * It is the other I2C (CH32RV_WIRE1_INSTANCE, ch32rv_instances.h): its row of
 * the hardware driver's table, or with that I2C set to Software, a SoftWire
 * on its default pins. */
#include "Wire.h"

#if CH32RV_WIRE_COUNT > 1
#if !CH32RV_WIRE1_SOFT
arduino::CH32RVTwoWire Wire1(CH32RV_WIRE1_ROW);
#elif CH32RV_WIRE1_INSTANCE == 1 && defined(CH32RV_I2C1_SDA)
arduino::SoftWire Wire1(CH32RV_I2C1_SDA, CH32RV_I2C1_SCL);
#elif CH32RV_WIRE1_INSTANCE == 2 && defined(CH32RV_I2C2_SDA)
arduino::SoftWire Wire1(CH32RV_I2C2_SDA, CH32RV_I2C2_SCL);
#else
arduino::SoftWire Wire1(CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN);
#endif
#endif
