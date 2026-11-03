/* What the hardware and the software SPI share. */
#include "SPIClass.h"

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

void SPIClass::transfer(void *buf, size_t count)
{
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++) {
        p[i] = transfer(p[i]);
    }
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

void SPIClass::transferBytes(const uint8_t *data, uint8_t *out, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        const uint8_t in = transfer(data ? data[i] : 0xFFu);
        if (out) {
            out[i] = in;
        }
    }
}
