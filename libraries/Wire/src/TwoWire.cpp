/* What the hardware and the software bus share. */
#include "TwoWire.h"

/* SMBus PEC (TwoWirePec.cpp). Weak: the library is an archive, and a weak
 * reference does not pull a member out of one, so only a sketch calling
 * setPEC() - which is in that file - links them; _pec is never set without it. */
uint8_t twowire_pec_tx(TwoWire &bus, bool stop) __attribute__((weak));
size_t twowire_pec_rx(TwoWire &bus, uint16_t address, size_t len, bool stop) __attribute__((weak));

bool TwoWire::begin(int sda, int scl, uint32_t frequency)
{
    if (_started) {
        end();
    }
    if (!(sda < 0 && scl < 0) && !setPins(sda, scl)) {
        return false;
    }
    if (frequency && !setClock(frequency)) {
        return false;
    }
    begin();
    return true;
}

bool TwoWire::prepare(int sda, int scl, uint32_t frequency)
{
    if (_started) {
        end();
    }
    if (!(sda < 0 && scl < 0) && !setPins(sda, scl)) {
        return false;
    }
    if (frequency && !setClock(frequency)) {
        return false;
    }
    return true;
}

void TwoWire::beginTransmission(uint16_t address)
{
    _address = address;
    _tx_len = 0;
    _tx_overflow = false;
    _transmitting = true;
}

uint8_t TwoWire::endTransmission(bool stopBit)
{
    if (!_transmitting || !_started) {
        /* A bus that is not open has nothing to drive. (A target may be a
         * controller too: the hardware Wire's transmit() waits for the bus;
         * SoftWire's refuses, 4.) */
        _transmitting = false;
        return 4;
    }
    _transmitting = false;
    if (_tx_overflow) {
        _tx_overflow = false;
        _tx_len = 0;
        return 1;                        /* AVR's "data too long" */
    }
    const uint8_t rc = _pec ? twowire_pec_tx(*this, stopBit) : transmit(stopBit);
    _tx_len = 0;
    return rc;
}

size_t TwoWire::requestFrom(uint16_t address, size_t len, bool stopBit)
{
    _rx_len = 0;
    _rx_read = 0;
    if (!_started || len == 0) {
        return 0;
    }
    if (len > CH32RV_WIRE_BUFFER_SIZE) {
        len = CH32RV_WIRE_BUFFER_SIZE;
    }
    const size_t got = _pec ? twowire_pec_rx(*this, address, len, stopBit) : receive(address, len, stopBit);
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
