/* Bit-banged I2C controller for CH32 RISC-V.
 *
 * The same API as <Wire.h>, on any two pads. The hardware I2C can only reach
 * the pads its routes name, and on the small parts those are often gone -
 * not bonded out on your package, or already carrying something else.
 *
 * It derives from HardwareI2C, so anything written against `TwoWire&` takes
 * one of these unchanged:
 *
 *   SoftWire bus(PA1, PA2);        // SDA, SCL
 *   Adafruit_Something dev(&bus);
 *
 * I2C is the bus where bit-banging costs least: it is open-drain and slow by
 * design, and the controller owns the clock, so running under the nominal
 * 100 kHz is not a protocol violation - it is just a slower transfer.
 *
 * Both pads are driven as OUTPUT_OPENDRAIN, which CH32's GPIO has natively, so
 * HIGH is a genuine release to the pull-ups rather than a switch to INPUT.
 * **The bus still needs pull-ups**; nothing here can substitute for them.
 *
 * Controller (master) only. A target has to answer someone else's clock, which
 * a busy loop cannot promise.
 */
#pragma once

#include "api/HardwareI2C.h"
#include "ch32_pins.h"

#include <stdint.h>

#ifndef SOFTWIRE_BUFFER_SIZE
#define SOFTWIRE_BUFFER_SIZE 32
#endif

/* How long one wait for the bus may take before the call gives up. The same
 * 25 ms <Wire.h> uses, and the same number AVR's own default uses. */
#ifndef SOFTWIRE_TIMEOUT_US
#define SOFTWIRE_TIMEOUT_US 25000UL
#endif

namespace arduino {

class SoftWire : public HardwareI2C {
public:
    SoftWire(uint8_t sda, uint8_t scl) : _sda(sda), _scl(scl) {}

    void begin() override;
    /* Target mode is not possible here - see the note at the top. Accepted so
     * that code written for another core still compiles; the bus stays a
     * controller and nothing answers at `address`. */
    void begin(uint8_t address) override { (void)address; begin(); }
    void end() override;

    /* Sets the floor on the half period, not an achieved frequency: a
     * bit-banged bus cannot hit a number. Anything at or above what the loop
     * already produces leaves it running flat out. */
    void setClock(uint32_t freq) override;

    void beginTransmission(uint8_t address) override;
    uint8_t endTransmission(bool stopBit) override;
    uint8_t endTransmission(void) override { return endTransmission(true); }

    size_t requestFrom(uint8_t address, size_t len, bool stopBit) override;
    size_t requestFrom(uint8_t address, size_t len) override {
        return requestFrom(address, len, true);
    }

    /* No target mode, so no callbacks can ever fire. */
    void onReceive(void (*)(int)) override {}
    void onRequest(void (*)(void)) override {}

    size_t write(uint8_t data) override;
    size_t write(const uint8_t *data, size_t len) override;
    using Print::write;

    int available(void) override;
    int read(void) override;
    int peek(void) override;
    void flush(void) override {}

    /* AVR's timeout API, and the same two divergences <Wire.h> documents: the
     * timeout is ON by default, and the bus is released either way. */
    void setWireTimeout(uint32_t timeout = SOFTWIRE_TIMEOUT_US,
                        bool reset_with_timeout = false);
    bool getWireTimeoutFlag(void) { return _timeout_flag; }
    void clearWireTimeoutFlag(void) { _timeout_flag = false; }

private:
    void hold(uint8_t pin);            /* drive low                     */
    void release(uint8_t pin);         /* let the pull-up take it high  */
    bool scl_high(void);               /* release SCL, honour stretching */
    void half(void);

    void start_condition(void);
    void stop_condition(void);
    bool write_byte(uint8_t b);        /* true when the target ACKed    */
    uint8_t read_byte(bool ack);

    const uint8_t _sda;
    const uint8_t _scl;

    uint8_t _tx[SOFTWIRE_BUFFER_SIZE];
    uint8_t _rx[SOFTWIRE_BUFFER_SIZE];
    uint8_t _tx_len = 0;
    uint8_t _rx_len = 0;
    uint8_t _rx_read = 0;
    uint8_t _address = 0;

    uint16_t _half_us = 5;             /* ~100 kHz if the loop were free */
    uint32_t _timeout_us = SOFTWIRE_TIMEOUT_US;
    bool _timeout_flag = false;
    bool _transmitting = false;
    bool _tx_overflow = false;
    bool _started = false;
};

}  // namespace arduino

using arduino::SoftWire;
