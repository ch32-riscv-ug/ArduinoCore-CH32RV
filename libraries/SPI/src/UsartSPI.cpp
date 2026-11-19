#include "UsartSPI.h"


namespace arduino {

namespace {

/* The USART sends the least significant bit first, always. */
inline uint8_t reverse(uint8_t b)
{
    return __builtin_bitreverse8(b);
}

}  // namespace

bool UsartSPI::open(void)
{
    _started = _port.beginSynchronous(_clock, _mode);
    return _started;
}

void UsartSPI::begin()
{
    (void)open();
}

bool UsartSPI::begin(int sck, int miso, int mosi, int ss)
{
    (void)ss;
    end();
    if (!(sck < 0 && miso < 0 && mosi < 0)) {
        if (miso < 0 || mosi < 0 || !_port.setPins(miso, mosi)) {
            return false;
        }
    }
    if (!open()) {
        return false;
    }
    /* SCK is whatever CK the route has: a different pad named is refused. */
    if (sck >= 0 && _port.synchronousClockPin() != (int)CH32RV_PIN_RESOLVE(sck)) {
        end();
        return false;
    }
    return true;
}

void UsartSPI::end()
{
    if (_started) {
        _port.endSynchronous();
        _started = false;
    }
}

void UsartSPI::setDataMode(uint8_t mode)
{
    mode &= 3u;
    if (mode != _mode) {
        _mode = mode;
        if (_started) {
            (void)open();
        }
    }
}

void UsartSPI::beginTransaction(SPISettings settings)
{
    _order = settings.getBitOrder();
    const uint32_t clock = settings.getClockFreq();
    const uint8_t mode = (uint8_t)settings.getDataMode() & 3u;
    if (settings.getBusMode() != SPI_CONTROLLER) {
        end();                          /* no peripheral mode on a USART */
        return;
    }
    if (!_started || clock != _clock || mode != _mode) {
        _clock = clock;
        _mode = mode;
        (void)open();
    }
}

uint8_t UsartSPI::transfer(uint8_t data)
{
    if (!_started) {
        return 0;
    }
    if (_order == MSBFIRST) {
        return reverse(_port.transferSynchronous(reverse(data)));
    }
    return _port.transferSynchronous(data);
}

}  // namespace arduino
