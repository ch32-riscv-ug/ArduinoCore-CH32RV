/* SPI on its own: the linker takes this file only when a sketch names it.
 * It is row 0 of the enabled SPIs (ch32rv_instances.h). */
#include "SPI.h"

#if CH32RV_SPI_COUNT > 0
arduino::CH32RVSPIClass SPI(0);
#endif
