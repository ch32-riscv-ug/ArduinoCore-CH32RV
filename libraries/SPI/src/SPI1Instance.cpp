/* SPI1 (the second bus) on its own. */
#include "SPI.h"

#if defined(CH32RV_SPI1_SCK) && defined(CH32RV_SPI2_SCK)
arduino::CH32RVSPIClass SPI1(2);
#elif !defined(CH32RV_SPI1_SCK) && defined(CH32RV_SPI2_SCK) && defined(CH32RV_SPI3_SCK)
arduino::CH32RVSPIClass SPI1(3);
#endif
