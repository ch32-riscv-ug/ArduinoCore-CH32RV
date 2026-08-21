/* Wire (I2C) for CH32 RISC-V.
 *
 * Master mode, polled. The pins come from the variant's CH32_I2Cn_SCL/SDA, so
 * a sketch calls Wire.begin() with no arguments the way it does on AVR.
 *
 * Slave mode (begin(address), onReceive, onRequest) is declared because
 * ArduinoCore-API requires it, but it is not implemented yet: the calls are
 * accepted and do nothing rather than pretending to work. See docs/todo.ja.md.
 *
 * Every wait in the driver is bounded. A missing pull-up or a device holding
 * SDA low is the normal I2C failure, and an unbounded wait would turn that
 * into a hung sketch instead of an endTransmission() error code.
 */
#pragma once

#include "api/HardwareI2C.h"
#include "ch32_pins.h"
#include "ch32_route.h"
#include "pins_arduino.h"

#include <stdint.h>

/* AVR's Wire uses 32, and libraries written against it assume a write of more
 * than 32 bytes is truncated rather than sent. Raise it per sketch with
 *   -DCH32_WIRE_BUFFER_SIZE=128
 * remembering that both buffers grow, on every Wire instance. */
#ifndef CH32_WIRE_BUFFER_SIZE
#define CH32_WIRE_BUFFER_SIZE 32
#endif

/* How long a single bus wait may take before the call gives up, in
 * microseconds. 25 ms is well past the worst case for a byte at the slowest
 * clock this driver programs, so hitting it means the bus is stuck. */
#ifndef CH32_WIRE_TIMEOUT_US
#define CH32_WIRE_TIMEOUT_US 25000UL
#endif

namespace arduino {

class CH32TwoWire : public HardwareI2C {
public:
    CH32TwoWire(uint32_t base, uint32_t clock_bit, uint8_t scl_pin,
                uint8_t sda_pin, uint32_t remap_mask, uint32_t remap_value,
                uint32_t remap2_mask, uint32_t remap2_value)
        : _base(base), _clock_bit(clock_bit), _scl_pin(scl_pin),
          _sda_pin(sda_pin), _remap_mask(remap_mask),
          _remap_value(remap_value), _remap2_mask(remap2_mask),
          _remap2_value(remap2_value) {}

    void begin() override;
    void begin(uint8_t address) override;
    void end() override;

    void setClock(uint32_t freq) override;

    void beginTransmission(uint8_t address) override;
    uint8_t endTransmission(bool stopBit) override;
    uint8_t endTransmission(void) override { return endTransmission(true); }

    size_t requestFrom(uint8_t address, size_t len, bool stopBit) override;
    size_t requestFrom(uint8_t address, size_t len) override {
        return requestFrom(address, len, true);
    }

    void onReceive(void (*)(int)) override;
    void onRequest(void (*)(void)) override;

    /* Move this bus onto another of its pin routes.
     *
     * false, and nothing changed, when the route does not exist on this
     * series - and setPins() also refuses an SCL and an SDA that belong to
     * different routes, which the hardware cannot do. Several X035 routes
     * swap the two signals over the same pair of pads, so the order matters
     * and is checked.
     *
     * Calling either after begin() reopens the bus on the new pins and hands
     * the old pads back as inputs. */
    bool setRoute(uint8_t route);
    bool setPins(uint8_t scl, uint8_t sda);

    /* Print/Stream */
    size_t write(uint8_t data) override;
    size_t write(const uint8_t *data, size_t len) override;
    using Print::write;
    int available(void) override;
    int read(void) override;
    int peek(void) override;
    void flush(void) override {}

private:
    bool wait_flag1(uint16_t mask, bool set);
    bool use_route(const ch32_route_t &route);
    bool start(uint8_t address, bool read);
    void stop(void);
    void recover(void);

    const uint32_t _base;
    const uint32_t _clock_bit;
    /* Not const: setRoute()/setPins() move the bus between pin sets. */
    uint8_t _scl_pin;
    uint8_t _sda_pin;
    /* AFIO field that routes this I2C to _scl_pin/_sda_pin, written on every
     * begin() for the same reason HardwareSerial writes its own: going back to
     * the default pins has to be a real write, not an assumption. */
    const uint32_t _remap_mask;
    uint32_t _remap_value;
    const uint32_t _remap2_mask;
    uint32_t _remap2_value;

    uint32_t _clock_hz = 100000;
    uint8_t _address = 0;          /* target of the transmission being built */
    bool _started = false;
    bool _transmitting = false;
    bool _tx_overflow = false;
    /* Set when a transfer ended badly, so the next one resets the peripheral
     * first: an aborted transfer can leave BUSY asserted forever. */
    bool _needs_recovery = false;

    uint8_t _tx[CH32_WIRE_BUFFER_SIZE];
    uint8_t _tx_len = 0;
    uint8_t _rx[CH32_WIRE_BUFFER_SIZE];
    uint8_t _rx_len = 0;
    uint8_t _rx_read = 0;
};

}  // namespace arduino

/* The bare name is the first bus and Wire1 the second, as elsewhere in the
 * Arduino ecosystem - see the note above the instances in Wire.cpp. */
#if defined(CH32_I2C1_SCL)
extern arduino::CH32TwoWire Wire;       /* I2C1 */
#if defined(CH32_I2C2_SCL)
extern arduino::CH32TwoWire Wire1;      /* I2C2 */
#endif
#elif defined(CH32_I2C2_SCL)
extern arduino::CH32TwoWire Wire;       /* I2C2, on a part that has no I2C1 */
#endif
