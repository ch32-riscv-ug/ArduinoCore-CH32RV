/* SPI2 (the third bus) on its own. */
#include "SPI.h"

#if defined(CH32RV_SPI1_SCK) && defined(CH32RV_SPI3_SCK)
arduino::CH32RVSPIClass SPI2(3);
#endif
