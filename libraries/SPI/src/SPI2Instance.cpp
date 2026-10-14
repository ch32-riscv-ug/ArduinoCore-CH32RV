/* SPI2 on its own: the linker takes this file only when a sketch names it.
 * It is row 2 of the enabled SPIs (ch32rv_instances.h). */
#include "SPI.h"

#if CH32RV_SPI_COUNT > 2
arduino::CH32RVSPIClass SPI2(2);
#endif
