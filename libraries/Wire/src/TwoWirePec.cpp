/* SMBus packet error checking (TwoWire::setPEC()), in a file of its own: the
 * transfers call into it through weak references (TwoWire.cpp), so only a
 * sketch that calls setPEC() links it.
 *
 * The CRC is SMBus's CRC-8, polynomial x^8 + x^2 + x + 1 (0x07), initial 0,
 * over every byte of the transaction as it is on the wire - the address bytes
 * (with their R/W bit) included - from the first START to the PEC byte. */
#include "TwoWire.h"

uint8_t TwoWire::pec(uint8_t crc, const uint8_t *data, size_t len)
{
    while (len--) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (uint8_t)((crc & 0x80u) ? (crc << 1) ^ 0x07u : crc << 1);
        }
    }
    return crc;
}

void TwoWire::setPEC(bool enable)
{
    _pec = enable ? PEC_ON : 0u;
}

namespace {

/* The address bytes of a transfer to `address` in direction `read`: one for a
 * 7-bit address; for a 10-bit one the header and the low byte, and a read
 * turns round with a repeated START and the header with R - unless the
 * transfer continues a write to it (the carried CRC), which sent the first two. */
uint8_t address_crc(uint8_t crc, uint16_t address, bool read, bool continued)
{
    if (!(address & CH32RV_WIRE_10BIT_BIT)) {
        const uint8_t a = (uint8_t)(((address & 0x7Fu) << 1) | (read ? 1u : 0u));
        return TwoWire::pec(crc, &a, 1);
    }
    const uint8_t header = (uint8_t)(0xF0u | ((address >> 7) & 0x06u));
    if (!(read && continued)) {
        const uint8_t w[2] = {header, (uint8_t)address};
        crc = TwoWire::pec(crc, w, 2);
    }
    if (read) {
        const uint8_t r = (uint8_t)(header | 1u);
        crc = TwoWire::pec(crc, &r, 1);
    }
    return crc;
}

}  // namespace

/* endTransmission() with PEC on: the write, its PEC byte appended when it
 * ends with a STOP (1, nothing sent, when the buffer has no room for it);
 * without one, its CRC is carried into the next transfer of the transaction. */
uint8_t twowire_pec_tx(TwoWire &bus, bool stop)
{
    const bool continued = (bus._pec & TwoWire::PEC_CARRY) != 0;
    uint8_t crc = address_crc(continued ? bus._pec_crc : 0u, bus._address, false, continued);
    crc = TwoWire::pec(crc, bus._tx, bus._tx_len);
    bus._pec = TwoWire::PEC_ON;
    if (stop) {
        if (bus._tx_len >= CH32RV_WIRE_BUFFER_SIZE) {
            return 1;
        }
        bus._tx[bus._tx_len] = crc;
        bus._tx_len = (uint8_t)(bus._tx_len + 1u);
    }
    const uint8_t rc = bus.transmit(stop);
    /* Carried only while the bus stays ours: a failed transfer ended the
     * transaction, and the next one starts afresh. */
    if (!stop && rc == 0) {
        bus._pec = TwoWire::PEC_ON | TwoWire::PEC_CARRY;
        bus._pec_crc = crc;
    }
    return rc;
}

/* requestFrom() with PEC on: `len` data bytes (one fewer when the buffer
 * has no room for the PEC byte after them) and the device's PEC byte; how
 * many data bytes the sketch gets - 0 when the read came short or the PEC
 * does not match. */
size_t twowire_pec_rx(TwoWire &bus, uint16_t address, size_t len, bool stop)
{
    if (len >= CH32RV_WIRE_BUFFER_SIZE) {
        len = CH32RV_WIRE_BUFFER_SIZE - 1u;
    }
    const bool continued = (bus._pec & TwoWire::PEC_CARRY) != 0;
    uint8_t crc = address_crc(continued ? bus._pec_crc : 0u, address, true, continued);
    bus._pec = TwoWire::PEC_ON;
    if (bus.receive(address, len + 1u, stop) != len + 1u) {
        return 0;
    }
    crc = TwoWire::pec(crc, bus._rx, len);
    return crc == bus._rx[len] ? len : 0u;
}
