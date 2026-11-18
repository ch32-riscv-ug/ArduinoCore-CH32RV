/* What the hardware and the software SPI share. */
#include "SPIClass.h"

const CH32RVSPIPeripheralOps *ch32rv_spi_peripheral;

uint16_t SPIClass::transfer16(uint16_t data)
{
    if (_order == LSBFIRST) {
        const uint16_t low = transfer((uint8_t)(data & 0xFFu));
        const uint16_t high = transfer((uint8_t)(data >> 8));
        return (uint16_t)(low | (high << 8));
    }
    const uint16_t high = transfer((uint8_t)(data >> 8));
    const uint16_t low = transfer((uint8_t)(data & 0xFFu));
    return (uint16_t)((high << 8) | low);
}


uint32_t SPIClass::transfer32(uint32_t data)
{
    if (_order == LSBFIRST) {
        const uint32_t lo = transfer16((uint16_t)data);
        const uint32_t hi = transfer16((uint16_t)(data >> 16));
        return (hi << 16) | lo;
    }
    const uint32_t hi = transfer16((uint16_t)(data >> 16));
    const uint32_t lo = transfer16((uint16_t)data);
    return (hi << 16) | lo;
}

void SPIClass::transfer_block(const uint8_t *tx, uint8_t *rx, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        const uint8_t in = transfer(tx ? tx[i] : 0xFFu);
        if (rx) {
            rx[i] = in;
        }
    }
}
