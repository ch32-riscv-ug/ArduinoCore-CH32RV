#include "SoftSPI.h"

#include "Arduino.h"

namespace arduino {

/* The clock rests at CPOL between transfers, and every bit leaves it there
 * again, so back-to-back transfer() calls need no fixing up in between. */
void SoftSPI::idle_clock(void)
{
    digitalWrite(_sck, _cpol ? HIGH : LOW);
}

void SoftSPI::begin()
{
    pinMode(_sck, OUTPUT);
    idle_clock();
    pinMode(_mosi, OUTPUT);
    digitalWrite(_mosi, LOW);
    if (_miso != NOT_A_PIN) {
        /* Floating, not pulled up: a pull-up would read 0xFF on an idle bus
         * and look like a device answering. <SPI.h> leaves MISO alone for the
         * same reason. */
        pinMode(_miso, INPUT);
    }
    _started = true;
}

bool SoftSPI::begin(int sck, int miso, int mosi, int ss)
{
    (void)ss;
    if (!(sck < 0 && miso < 0 && mosi < 0)) {
        if (sck < 0 || mosi < 0) {
            return false;
        }
        /* Pads from here on: every bit toggled later skips the board-number lookup. */
        sck = CH32RV_PIN_RESOLVE(sck);
        mosi = CH32RV_PIN_RESOLVE(mosi);
        if (miso >= 0) {
            miso = CH32RV_PIN_RESOLVE(miso);
        }
        if (!ch32rv_pad_is_valid(sck) || !ch32rv_pad_is_valid(mosi) ||
            (miso >= 0 && !ch32rv_pad_is_valid(miso))) {
            return false;
        }
        end();
        _sck = (uint8_t)sck;
        _mosi = (uint8_t)mosi;
        _miso = miso < 0 ? (uint8_t)NOT_A_PIN : (uint8_t)miso;
    }
    begin();
    return true;
}

uint32_t SoftSPI::transfer32(uint32_t data)
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

void SoftSPI::transferBytes(const uint8_t *data, uint8_t *out, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        const uint8_t in = transfer(data ? data[i] : 0xFFu);
        if (out) {
            out[i] = in;
        }
    }
}

void SoftSPI::end()
{
    if (!_started) {
        return;
    }
    /* Hand the pads back as inputs, so the next owner of the pin - another
     * bus, or a sketch driving it itself - starts from a known state. */
    pinMode(_sck, INPUT);
    pinMode(_mosi, INPUT);
    _started = false;
}

void SoftSPI::setDataMode(uint8_t mode)
{
    _cpol = (mode & 0x02) != 0;
    _cpha = (mode & 0x01) != 0;
    if (_started) {
        idle_clock();
    }
}

void SoftSPI::beginTransaction(SPISettings settings)
{
    /* getClockFreq() is deliberately not read: this bus cannot hit a
     * frequency, and pretending to would be worse than saying so. See
     * setHalfPeriodUs(). */
    _order = settings.getBitOrder();
    setDataMode((uint8_t)settings.getDataMode());
    if (!_started) {
        begin();
    }
}

void SoftSPI::endTransaction(void)
{
    idle_clock();
}

uint8_t SoftSPI::transfer(uint8_t data)
{
    const uint8_t active = _cpol ? LOW : HIGH;
    const uint8_t idle = _cpol ? HIGH : LOW;
    const bool msb = (_order == MSBFIRST);
    const uint16_t half = _half_us;
    const bool read_back = (_miso != NOT_A_PIN);
    uint8_t mask = msb ? 0x80u : 0x01u;
    uint8_t in = 0;

    for (uint8_t i = 0; i < 8; i++) {
        if (!_cpha) {
            /* Mode 0/2: the data is put up first and both ends sample on the
             * leading edge. */
            digitalWrite(_mosi, (data & mask) ? HIGH : LOW);
            if (half) {
                delayMicroseconds(half);
            }
            digitalWrite(_sck, active);
            if (read_back && digitalRead(_miso) == HIGH) {
                in |= mask;
            }
            if (half) {
                delayMicroseconds(half);
            }
            digitalWrite(_sck, idle);
        } else {
            /* Mode 1/3: the leading edge only opens the slot; the data is put
             * up inside it and sampled on the trailing edge. */
            digitalWrite(_sck, active);
            digitalWrite(_mosi, (data & mask) ? HIGH : LOW);
            if (half) {
                delayMicroseconds(half);
            }
            digitalWrite(_sck, idle);
            if (read_back && digitalRead(_miso) == HIGH) {
                in |= mask;
            }
            if (half) {
                delayMicroseconds(half);
            }
        }
        mask = msb ? (uint8_t)(mask >> 1) : (uint8_t)(mask << 1);
    }
    return in;
}

uint16_t SoftSPI::transfer16(uint16_t data)
{
    /* Big-endian on the wire, as <SPI.h> and every other core does it, and
     * independently of bit order - which reorders the bits inside a byte, not
     * the bytes inside a word. */
    const uint8_t hi = transfer((uint8_t)(data >> 8));
    const uint8_t lo = transfer((uint8_t)data);
    return (uint16_t)((uint16_t)hi << 8 | lo);
}

void SoftSPI::transfer(void *buf, size_t count)
{
    uint8_t *p = (uint8_t *)buf;
    while (count--) {
        *p = transfer(*p);
        p++;
    }
}

}  // namespace arduino
