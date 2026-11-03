/* TwoWire: the I2C controller type every I2C library takes.
 *
 * Libraries are written against `TwoWire *` / `TwoWire &` (Adafruit_BusIO,
 * U8g2, SparkFun, M5 ...), and some forward-declare it as `class TwoWire;`,
 * which only compiles when TwoWire is a class at global scope - not a typedef,
 * and not inside `namespace arduino`. So it is one here.
 *
 * It is the common base of the hardware bus (<Wire.h>: CH32RVTwoWire, which
 * Wire and Wire1 are) and the bit-banged one (<SoftWire.h>), so a library
 * handed either works unchanged, as a library handed a Stream takes any
 * Serial. The base also carries what both share - the transmit and receive
 * buffers, the Stream side, the timeout - and leaves opening the bus and the
 * transfers themselves to the two. The interface is ArduinoCore-API's
 * HardwareI2C plus what arduino-esp32's TwoWire adds, with its names and
 * argument order (SDA before SCL), so code written for an ESP32 carries over:
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

#include "Arduino.h"
#include "api/HardwareI2C.h"

#include <stdint.h>

/* Feature macros libraries test for (AVR's Wire and arduino-esp32 define them). */
#define WIRE_HAS_END 1
#define WIRE_HAS_TIMEOUT 1

/* AVR's Wire uses 32, and libraries written against it assume a write of more
 * than 32 bytes is truncated rather than sent. Raise it per sketch with
 *   -DCH32RV_WIRE_BUFFER_SIZE=128
 * remembering that both buffers grow, on every bus, hardware or software. */
#ifndef CH32RV_WIRE_BUFFER_SIZE
#define CH32RV_WIRE_BUFFER_SIZE 32
#endif
/* The names libraries size their transfers by (arduino-esp32 / AVR spellings). */
#define I2C_BUFFER_LENGTH CH32RV_WIRE_BUFFER_SIZE
#define BUFFER_LENGTH CH32RV_WIRE_BUFFER_SIZE

/* How long a single bus wait may take before the call gives up, in
 * microseconds. 25 ms is well past the worst case for a byte at the slowest
 * clock this driver programs, so hitting it means the bus is stuck. */
#ifndef CH32RV_WIRE_TIMEOUT_US
#define CH32RV_WIRE_TIMEOUT_US 25000UL
#endif

class TwoWire : public arduino::HardwareI2C {
public:
    /* Keep HardwareI2C's begin() / begin(address) visible next to the pin forms. */
    using arduino::HardwareI2C::begin;

    /* Controller on these pins (SDA, SCL). -1, -1 keeps the pins the bus has
     * (the board's default, or the last setPins() / setRoute()); one -1 alone
     * is refused. frequency 0 keeps the clock. false, and the bus left closed,
     * when the pins cannot carry this bus. */
    bool begin(int sda, int scl, uint32_t frequency = 0);
    /* Target at `address` on these pins. A bus that cannot be a target (SoftWire)
     * opens as a controller, the same as its begin(address). */
    bool begin(uint8_t address, int sda, int scl, uint32_t frequency);
    /* Move the bus to these pins (SDA, SCL); an open bus reopens on them. */
    virtual bool setPins(int sda, int scl) = 0;

    uint32_t getClock(void) { return _clock_hz; }

    /* AVR's timeout API (Wire.h since 1.8.x), with two documented differences.
     *
     * The timeout is ON by default here, at CH32RV_WIRE_TIMEOUT_US (25 ms, the
     * same number AVR's own default uses). On AVR it is off until a sketch
     * asks for it, and a stuck bus hangs the sketch forever; that is not a
     * behaviour worth reproducing, so the default stays on. Pass 0 to turn it
     * off and get AVR's original behaviour.
     *
     * reset_with_timeout is accepted and the bus is reset either way: the
     * hardware bus marks the peripheral for recovery whenever a wait ends in
     * anything but a NACK, and a stuck software bus ends in a STOP. */
    void setWireTimeout(uint32_t timeout_us = CH32RV_WIRE_TIMEOUT_US, bool reset_with_timeout = false)
    {
        (void)reset_with_timeout;
        _timeout_us = timeout_us;
    }
    /* Sticky: set by any wait that ran out, and only cleared by the call
     * below, so a sketch can check once after a burst of transfers. */
    bool getWireTimeoutFlag(void) { return _timeout_flag; }
    void clearWireTimeoutFlag(void) { _timeout_flag = false; }
    /* arduino-esp32's timeout, in milliseconds. */
    void setTimeOut(uint16_t timeout_ms) { setWireTimeout((uint32_t)timeout_ms * 1000u); }
    uint16_t getTimeOut(void) { return (uint16_t)(_timeout_us / 1000u); }

    void beginTransmission(uint8_t address) override;
    /* 0 sent, 1 more than CH32RV_WIRE_BUFFER_SIZE, 2 address NACK, 3 data
     * NACK, 4 the bus is not open (or is a target), 5 timeout. */
    uint8_t endTransmission(bool stopBit) override;
    uint8_t endTransmission(void) override { return endTransmission(true); }
    size_t requestFrom(uint8_t address, size_t len, bool stopBit) override;
    size_t requestFrom(uint8_t address, size_t len) override { return requestFrom(address, len, true); }

    size_t write(uint8_t data) override;
    size_t write(const uint8_t *data, size_t len) override;
    /* AVR's Wire has these, so `Wire.write(0)` and `Wire.write(someInt)` pick
     * the byte write instead of being ambiguous with Print::write(const char *). */
    using arduino::Print::write;
    inline size_t write(unsigned long n) { return write((uint8_t)n); }
    inline size_t write(long n) { return write((uint8_t)n); }
    inline size_t write(unsigned int n) { return write((uint8_t)n); }
    inline size_t write(int n) { return write((uint8_t)n); }
    int available(void) override;
    int read(void) override;
    int peek(void) override;
    void flush(void) override {}

protected:
    /* The transfers, on an open bus that is a controller. transmit() sends
     * _tx[0.._tx_len) to _address and returns endTransmission()'s code;
     * receive() reads len (1..CH32RV_WIRE_BUFFER_SIZE) bytes into _rx and
     * returns how many arrived. */
    virtual uint8_t transmit(bool stop) = 0;
    virtual size_t receive(uint8_t address, size_t len, bool stop) = 0;

    uint32_t _clock_hz = 100000;
    /* 0 disables the timeout, as on AVR. */
    uint32_t _timeout_us = CH32RV_WIRE_TIMEOUT_US;
    uint8_t _address = 0;          /* target of the transmission being built */
    bool _started = false;
    bool _transmitting = false;
    bool _tx_overflow = false;
    bool _timeout_flag = false;
    /* A target, or answering a read in onRequest(): the hardware bus only. */
    volatile bool _slave = false;
    volatile bool _slave_replying = false;

    /* Volatile for the halves a target's interrupt handler writes while the
     * sketch reads. */
    uint8_t _tx[CH32RV_WIRE_BUFFER_SIZE];
    volatile uint8_t _tx_len = 0;
    uint8_t _rx[CH32RV_WIRE_BUFFER_SIZE];
    volatile uint8_t _rx_len = 0;
    volatile uint8_t _rx_read = 0;
};
