/* SPI on its own: the linker takes this file only when a sketch names it. The
 * bare name is the first bus, as everywhere else in the Arduino ecosystem. */
#include "SPI.h"

#if defined(CH32RV_SPI1_SCK)
arduino::CH32RVSPIClass SPI(1);
#elif defined(CH32RV_SPI2_SCK)
arduino::CH32RVSPIClass SPI(2);      /* a part with no SPI1 */
#endif
