/* TwoWire: the I2C controller type every I2C library takes.
 *
 * Libraries are written against `TwoWire *` / `TwoWire &` (Adafruit_BusIO,
 * U8g2, SparkFun, M5 ...), and some forward-declare it as `class TwoWire;`,
 * which only compiles when TwoWire is a class at global scope - not a typedef,
 * and not inside `namespace arduino`. So it is one here.
 *
 * It is the common base of the hardware bus (<Wire.h>: Wire, Wire1) and the
 * bit-banged one (<SoftWire.h>), so a library handed either works unchanged.
 * The interface is ArduinoCore-API's HardwareI2C plus what arduino-esp32's
 * TwoWire adds, with its names and argument order (SDA before SCL), so code
 * written for an ESP32 carries over:
 *
 *   Wire.begin(PA10, PA11);             // SDA, SCL; -1, -1 keeps the pins
 *   Wire.begin(PA10, PA11, 400000);
 *   Wire.begin(0x42, PA10, PA11, 0);    // target at 0x42
 *   Wire.setPins(PA10, PA11);           // SDA, SCL
 *   Wire.setTimeOut(50);                // milliseconds
 *
 * AVR's timeout API (setWireTimeout, microseconds) is kept alongside.
 */
#pragma once

#include "api/HardwareI2C.h"

#include <stdint.h>

/* Feature macros libraries test for (AVR's Wire and arduino-esp32 define them). */
#define WIRE_HAS_END 1
#define WIRE_HAS_TIMEOUT 1

class TwoWire : public arduino::HardwareI2C {
public:
    /* Keep HardwareI2C's begin() / begin(address) visible next to the pin forms. */
    using arduino::HardwareI2C::begin;

    /* Controller on these pins (SDA, SCL). -1, -1 keeps the pins the bus has
     * (the variant's default, or the last setPins() / setRoute()); one -1 alone
     * is refused. frequency 0 keeps the clock. false, and the bus left closed,
     * when the pins cannot carry this bus. */
    virtual bool begin(int sda, int scl, uint32_t frequency = 0) = 0;
    /* Target at `address` on these pins. A bus that cannot be a target (SoftWire)
     * opens as a controller, the same as its begin(address). */
    virtual bool begin(uint8_t address, int sda, int scl, uint32_t frequency) = 0;
    /* Move the bus to these pins (SDA, SCL); an open bus reopens on them. */
    virtual bool setPins(int sda, int scl) = 0;

    virtual uint32_t getClock(void) = 0;

    /* arduino-esp32's timeout, in milliseconds. */
    void setTimeOut(uint16_t timeout_ms) { setWireTimeout((uint32_t)timeout_ms * 1000u); }
    uint16_t getTimeOut(void) { return (uint16_t)(wireTimeoutUs() / 1000u); }

    /* AVR's timeout, in microseconds; 0 turns it off. */
    virtual void setWireTimeout(uint32_t timeout_us = 25000UL, bool reset_with_timeout = false) = 0;
    virtual bool getWireTimeoutFlag(void) = 0;
    virtual void clearWireTimeoutFlag(void) = 0;

    /* AVR's Wire has these, so `Wire.write(0)` and `Wire.write(someInt)` pick
     * the byte write instead of being ambiguous with Print::write(const char *). */
    using arduino::Print::write;
    inline size_t write(unsigned long n) { return write((uint8_t)n); }
    inline size_t write(long n) { return write((uint8_t)n); }
    inline size_t write(unsigned int n) { return write((uint8_t)n); }
    inline size_t write(int n) { return write((uint8_t)n); }

protected:
    virtual uint32_t wireTimeoutUs(void) = 0;
};
