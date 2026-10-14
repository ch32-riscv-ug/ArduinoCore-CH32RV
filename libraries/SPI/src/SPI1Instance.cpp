/* SPI1 on its own: the linker takes this file only when a sketch names it.
 * It is row 1 of the enabled SPIs (ch32rv_instances.h). */
#include "SPI.h"

#if CH32RV_SPI_COUNT > 1
arduino::CH32RVSPIClass SPI1(1);
#endif
