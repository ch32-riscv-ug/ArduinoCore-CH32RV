/* Peripheral mode (SPI_HAS_PERIPHERAL_MODE) linked on demand: an SPISettings
 * built with SPI_PERIPHERAL calls ch32rv_spi_peripheral_linked() (the
 * ArduinoCore-API patch in HardwareSPI.h), which hands the buses the code
 * below; a sketch that never builds one references neither, and links none of
 * it - the buses only test a pointer and a flag. */
#include "SPI.h"

namespace {

const CH32RVSPIPeripheralOps ops = {
    [](arduino::CH32RVSPIClass &bus, bool on) { bus.peripheral_mode(on); },
    [](arduino::CH32RVSPIClass &bus, uint8_t data) { return bus.peripheral_transfer(data); },
    [](arduino::SoftSPI &bus, bool on) { bus.peripheral_mode(on); },
    [](arduino::SoftSPI &bus, uint8_t data) { return bus.follow(data); },
};

}  // namespace

namespace arduino {

void ch32rv_spi_peripheral_linked(void)
{
    ch32rv_spi_peripheral = &ops;
}

}  // namespace arduino
