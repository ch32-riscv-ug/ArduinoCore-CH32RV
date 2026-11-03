/* What the hardware and the software bus share. */
#include "TwoWire.h"

bool TwoWire::begin(int sda, int scl, uint32_t frequency)
{
    if (_started) {
        end();
    }
    if (!(sda < 0 && scl < 0) && !setPins(sda, scl)) {
        return false;
    }
    if (frequency) {
        _clock_hz = frequency;
    }
    begin();
    return true;
}

bool TwoWire::begin(uint8_t address, int sda, int scl, uint32_t frequency)
{
    if (_started) {
        end();
    }
    if (!(sda < 0 && scl < 0) && !setPins(sda, scl)) {
        return false;
    }
    if (frequency) {
        _clock_hz = frequency;
    }
    begin(address);
    return true;
}

void TwoWire::beginTransmission(uint8_t address)
{
    _address = address;
    _tx_len = 0;
    _tx_overflow = false;
    _transmitting = true;
}

uint8_t TwoWire::endTransmission(bool stopBit)
{
    if (!_transmitting || _slave || !_started) {
        /* A target starting a controller transfer would have to win the bus
         * from the controller that is addressing it; one role at a time. A
         * bus that is not open has nothing to drive. */
        _transmitting = false;
        return 4;
    }
    _transmitting = false;
    if (_tx_overflow) {
        _tx_overflow = false;
        _tx_len = 0;
        return 1;                        /* AVR's "data too long" */
    }
    const uint8_t rc = transmit(stopBit);
    _tx_len = 0;
    return rc;
}

size_t TwoWire::requestFrom(uint8_t address, size_t len, bool stopBit)
{
    _rx_len = 0;
    _rx_read = 0;
    if (!_started || _slave || len == 0) {
        return 0;
    }
    if (len > CH32RV_WIRE_BUFFER_SIZE) {
        len = CH32RV_WIRE_BUFFER_SIZE;
    }
    const size_t got = receive(address, len, stopBit);
    _rx_len = (uint8_t)got;
    return got;
}

size_t TwoWire::write(uint8_t data)
{
    if (!_transmitting && !_slave_replying) {
        return 0;        /* outside any transmission, bytes go nowhere */
    }
    if (_tx_len >= CH32RV_WIRE_BUFFER_SIZE) {
        /* AVR truncates and reports it from endTransmission() as 1, so the
         * flag has to survive until then. */
        _tx_overflow = true;
        return 0;
    }
    _tx[_tx_len] = data;
    _tx_len = (uint8_t)(_tx_len + 1u);
    return 1;
}

size_t TwoWire::write(const uint8_t *data, size_t len)
{
    size_t written = 0;
    for (size_t i = 0; i < len; i++) {
        if (!write(data[i])) {
            break;
        }
        written++;
    }
    return written;
}

int TwoWire::available(void)
{
    return _rx_len - _rx_read;
}

int TwoWire::read(void)
{
    if (_rx_read >= _rx_len) {
        return -1;
    }
    const uint8_t b = _rx[_rx_read];
    _rx_read = (uint8_t)(_rx_read + 1u);
    return b;
}

int TwoWire::peek(void)
{
    if (_rx_read >= _rx_len) {
        return -1;
    }
    return _rx[_rx_read];
}
