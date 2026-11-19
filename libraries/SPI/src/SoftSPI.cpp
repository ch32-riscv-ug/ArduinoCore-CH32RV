#include "SoftSPI.h"

#include "Arduino.h"
#include "internal/ch32rv_gpio.h"

namespace arduino {

/* The clock rests at CPOL between transfers, and every bit leaves it there
 * again, so back-to-back transfer() calls need no fixing up in between. */
void SoftSPI::idle_clock(void)
{
    digitalWrite(_sck, cpol() ? HIGH : LOW);
}

void SoftSPI::begin()
{
    /* No pins yet (a Software menu entry on a part with no default route):
     * begin(sck, miso, mosi) names them. */
    if (_sck == CH32RV_ROUTE_NO_PIN) {
        return;
    }
    controller_pads();
    _started = true;
}

void SoftSPI::controller_pads(void)
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
}

bool SoftSPI::setPins(int sck, int miso, int mosi)
{
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
    const bool was_started = _started;
    end();
    _sck = (uint8_t)sck;
    _mosi = (uint8_t)mosi;
    _miso = miso < 0 ? (uint8_t)NOT_A_PIN : (uint8_t)miso;
    if (was_started) {
        begin();
    }
    return true;
}

void SoftSPI::end()
{
    if (!_started) {
        return;
    }
    /* Hand the pads back as inputs, so the next owner of the pin - another
     * bus, or a sketch driving it itself - starts from a known state. */
    if (_periph) {
        ch32rv_spi_peripheral->soft_mode(*this, false);   /* MISO back to an input */
    }
    pinMode(_sck, INPUT);
    pinMode(_mosi, INPUT);
    _started = false;
}

void SoftSPI::setDataMode(uint8_t mode)
{
    _mode = (uint8_t)(mode & 0x03u);
    if (_started && !_periph) {
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
    if (!_started) {
        return;
    }
    if (settings.getBusMode() != SPI_CONTROLLER) {
        if (ch32rv_spi_peripheral) {
            ch32rv_spi_peripheral->soft_mode(*this, true);
        }
    } else if (_periph) {
        ch32rv_spi_peripheral->soft_mode(*this, false);
    }
}

void SoftSPI::endTransaction(void)
{
    if (_periph) {
        ch32rv_spi_peripheral->soft_mode(*this, false);
    }
    idle_clock();
}

void SoftSPI::peripheral_mode(bool on)
{
    if (!on) {
        controller_pads();
        _periph = false;
        return;
    }
    /* The controller's clock and data in, this end's data out. */
    pinMode(_sck, INPUT);
    pinMode(_mosi, INPUT);
    if (_miso != NOT_A_PIN) {
        pinMode(_miso, OUTPUT);
    }
    _periph = true;
}

/* A peripheral's byte: SCK polled for the controller's edges. CPHA 0: the bit
 * is up before the leading edge, both ends sample on it, the next goes up
 * after the trailing one. CPHA 1: up on the leading edge, sampled on the
 * trailing one. */
uint8_t SoftSPI::follow(uint8_t data)
{
    const bool msb = (_order == MSBFIRST);
    uint8_t mask = msb ? 0x80u : 0x01u;
    const bool drive = _miso != NOT_A_PIN;
    const pin_size_t miso = drive ? CH32RV_PIN_RESOLVE(_miso) : 0;
    const uint8_t miso_port = (uint8_t)CH32RV_PIN_PORT(miso), miso_bit = (uint8_t)CH32RV_PIN_BIT(miso);
    /* CPHA 0: the first bit up before anything else. Between two bytes the controller gives half a period from the
     * last trailing edge to the next leading one, and the return, the next transfer() and this call spend most of
     * it on a slow part (X035 at 48 MHz, 50 kHz: 0.2 us left, 2026-11-20). */
    if (drive && !cpha()) {
        if (data & mask) {
            ch32rv_gpio_set(miso_port, miso_bit);
        } else {
            ch32rv_gpio_clear(miso_port, miso_bit);
        }
    }
    const pin_size_t sck = CH32RV_PIN_RESOLVE(_sck), mosi = CH32RV_PIN_RESOLVE(_mosi);
    const uint8_t sck_port = (uint8_t)CH32RV_PIN_PORT(sck), sck_bit = (uint8_t)CH32RV_PIN_BIT(sck);
    const uint8_t mosi_port = (uint8_t)CH32RV_PIN_PORT(mosi), mosi_bit = (uint8_t)CH32RV_PIN_BIT(mosi);
    const uint8_t idle = cpol() ? 1u : 0u;
    uint8_t in = 0;

    for (uint8_t i = 0; i < 8; i++) {
        if (cpha()) {
            while (ch32rv_gpio_read(sck_port, sck_bit) == idle) {
            }
        }
        if (drive && (cpha() || i)) {
            if (data & mask) {
                ch32rv_gpio_set(miso_port, miso_bit);
            } else {
                ch32rv_gpio_clear(miso_port, miso_bit);
            }
        }
        if (!cpha()) {
            while (ch32rv_gpio_read(sck_port, sck_bit) == idle) {
            }
            if (ch32rv_gpio_read(mosi_port, mosi_bit)) {
                in |= mask;
            }
        }
        while (ch32rv_gpio_read(sck_port, sck_bit) != idle) {
        }
        if (cpha() && ch32rv_gpio_read(mosi_port, mosi_bit)) {
            in |= mask;
        }
        mask = msb ? (uint8_t)(mask >> 1) : (uint8_t)(mask << 1);
    }
    return in;
}

uint8_t SoftSPI::transfer(uint8_t data)
{
    if (_periph) {
        return ch32rv_spi_peripheral->soft_transfer(*this, data);
    }
    const uint8_t active = cpol() ? LOW : HIGH;
    const uint8_t idle = cpol() ? HIGH : LOW;
    const bool msb = (_order == MSBFIRST);
    const uint16_t half = _half_us;
    const bool read_back = (_miso != NOT_A_PIN);
    uint8_t mask = msb ? 0x80u : 0x01u;
    uint8_t in = 0;

    for (uint8_t i = 0; i < 8; i++) {
        if (!cpha()) {
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

}  // namespace arduino
