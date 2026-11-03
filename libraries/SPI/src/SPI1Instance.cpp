/* SPI1 on its own: the linker takes this file only when a sketch names it.
 * It is SPI number ROW1_N, the second that is on (ch32rv_instances.h):
 * its row of the hardware driver's table, or with that SPI set to Software,
 * a SoftSPI on its default pins. */
#include "SPI.h"

#if CH32RV_SPI_COUNT > 1
#if !CH32RV_SPI_SOFT_N(CH32RV_SPI_ROW1_N)
arduino::CH32RVSPIClass SPI1(CH32RV_SPI_HW_ROW(CH32RV_SPI_ROW1_N));
#elif CH32RV_SPI_ROW1_N == 1 && defined(CH32RV_SPI1_SCK)
arduino::SoftSPI SPI1(CH32RV_SPI1_SCK, CH32RV_SPI1_MISO, CH32RV_SPI1_MOSI);
#elif CH32RV_SPI_ROW1_N == 2 && defined(CH32RV_SPI2_SCK)
arduino::SoftSPI SPI1(CH32RV_SPI2_SCK, CH32RV_SPI2_MISO, CH32RV_SPI2_MOSI);
#elif CH32RV_SPI_ROW1_N == 3 && defined(CH32RV_SPI3_SCK)
arduino::SoftSPI SPI1(CH32RV_SPI3_SCK, CH32RV_SPI3_MISO, CH32RV_SPI3_MOSI);
#else
/* No default route on this part: begin(sck, miso, mosi) names the pins. */
arduino::SoftSPI SPI1(CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN);
#endif
#endif
