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
 * HardwareI2C's plus what arduino-esp32's TwoWire adds, with its names and
 * argument order (SDA before SCL), so code written for an ESP32 carries over.
 * It is not derived from HardwareI2C: that makes begin(address) virtual, and
 * a virtual function is linked whether called or not - the target's
 * interrupt handling came with every controller (504 B on V003). Becoming a
 * target is the bus object's own call (Wire.begin(0x42)), not the base's:
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
#include "api/Stream.h"

#include <stdint.h>

/* Feature macros libraries test for (AVR's Wire and arduino-esp32 define them). */
#define WIRE_HAS_END 1
#define WIRE_HAS_TIMEOUT 1

/* A 10-bit address, for beginTransmission() / requestFrom() and a target's
 * begin(address), on Wire and SoftWire alike (Linux's I2C_M_TEN, as a bit of
 * the address):
 *
 *   Wire.beginTransmission(0x2A5 | WIRE_10BIT);
 *   Wire.requestFrom(0x2A5 | WIRE_10BIT, 2);
 *   Wire.begin(0x2A5 | WIRE_10BIT);          // a target at 0x2A5
 *
 * On the wire it is the header 11110xx0 and the low byte, and a read turns
 * round with a repeated START and 11110xx1. Without the bit an address is a
 * 7-bit one, as everywhere in Arduino.
 *
 * WIRE_10BIT is that bit (CH32RV_WIRE_10BIT_BIT) as a constant defined in
 * Wire10Bit.cpp, beside the controllers' 10-bit addressing, which the
 * transfers reach through weak references: naming it is what links that
 * code, so a sketch that never does carries none of it. It is not a
 * compile-time constant (no `constexpr`, no `case` label); a value read from
 * it is. */
#define CH32RV_WIRE_10BIT_BIT 0x8000u
extern "C" const uint16_t ch32rv_wire_10bit;
#define WIRE_10BIT ch32rv_wire_10bit

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

class TwoWire : public arduino::Stream {
public:
    /* Controller on the bus's pins. */
    virtual void begin() = 0;
    /* Controller on these pins (SDA, SCL). -1, -1 keeps the pins the bus has
     * (the board's default, or the last setPins() / setRoute()); one -1 alone
     * is refused. frequency 0 keeps the clock. false, and the bus left closed,
     * when the pins cannot carry this bus or setClock() refuses frequency. */
    bool begin(int sda, int scl, uint32_t frequency = 0);
    virtual void end() = 0;
    /* The SCL clock, as arduino-esp32's: the bus never runs faster than it
     * (only slower - the lines' rise, a target's stretching, SoftWire's CPU).
     * false, and the clock kept, for 0 and for a clock slower than the bus can
     * make (it would run faster). Faster than the bus can make runs at the
     * fastest it makes. */
    virtual bool setClock(uint32_t freq) = 0;
    /* Move the bus to these pins (SDA, SCL); an open bus reopens on them. */
    virtual bool setPins(int sda, int scl) = 0;
    /* A target's callbacks (the bus object's begin(address) makes it one). On
     * the hardware Wire the bus stays a controller as well: a sketch may call
     * beginTransmission() / requestFrom() while it is a target (SoftWire's
     * target is one role at a time: 4 / 0 there). */
    virtual void onReceive(void (*)(int)) = 0;
    virtual void onRequest(void (*)(void)) = 0;

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

    /* address: 7 bits, or 10 with WIRE_10BIT. */
    void beginTransmission(uint16_t address);
    /* 0 sent, 1 more than CH32RV_WIRE_BUFFER_SIZE, 2 address NACK, 3 data
     * NACK, 4 the bus is not open (a SoftWire: or is a target) or another
     * controller kept winning the bus (lost arbitration on every try, AVR's
     * "other error"), 5 timeout. */
    uint8_t endTransmission(bool stopBit);
    uint8_t endTransmission(void) { return endTransmission(true); }
    size_t requestFrom(uint16_t address, size_t len, bool stopBit);
    size_t requestFrom(uint16_t address, size_t len) { return requestFrom(address, len, true); }

    /* SMBus packet error checking: with it on, endTransmission(true) sends
     * the CRC-8 (x^8 + x^2 + x + 1) of the whole transaction - the address
     * bytes included, from the first START - after the data, and
     * requestFrom(address, n) reads n bytes and the device's PEC byte and
     * returns 0, with nothing to read, when it does not match. A write ended
     * with endTransmission(false) carries its part of the CRC into the read
     * or write that follows (SMBus's read byte / word: command, repeated
     * START, data, PEC). Off by default; per bus. In software, the same on
     * Wire and SoftWire: one bit loop of 8 per byte, and only a sketch that
     * calls setPEC() links it (TwoWirePec.cpp). A target's PEC is the
     * sketch's (pec() below over what it got and what it sends). */
    void setPEC(bool enable);
    bool getPEC(void) const { return _pec != 0; }
    /* SMBus's CRC-8 of `len` bytes, continuing from `crc` (0 to start). */
    static uint8_t pec(uint8_t crc, const uint8_t *data, size_t len);

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
    friend uint8_t twowire_pec_tx(TwoWire &bus, bool stop);
    friend size_t twowire_pec_rx(TwoWire &bus, uint16_t address, size_t len, bool stop);

    /* begin(address, sda, scl, frequency)'s shared half: close an open bus,
     * move it to the pins (unless both are -1) and keep the clock; false when
     * the pins cannot carry this bus. */
    bool prepare(int sda, int scl, uint32_t frequency);

    /* The transfers, on an open bus that is a controller. transmit() sends
     * _tx[0.._tx_len) to _address and returns endTransmission()'s code;
     * receive() reads len (1..CH32RV_WIRE_BUFFER_SIZE) bytes into _rx and
     * returns how many arrived. */
    virtual uint8_t transmit(bool stop) = 0;
    virtual size_t receive(uint16_t address, size_t len, bool stop) = 0;

    uint32_t _clock_hz = 100000;
    /* 0 disables the timeout, as on AVR. */
    uint32_t _timeout_us = CH32RV_WIRE_TIMEOUT_US;
    uint16_t _address = 0;         /* target of the transmission being built (WIRE_10BIT) */
    bool _started = false;
    bool _transmitting = false;
    bool _tx_overflow = false;
    bool _timeout_flag = false;
    /* A target. */
    volatile bool _slave = false;

    /* Volatile for the halves a target's interrupt handler writes while the
     * sketch reads. (_tx at the offset it had before the 10-bit address and
     * PEC came - 31, a short load - and what they added after _rx: SoftWire's
     * target bit loop reads these, 2026-11-17.) */
    uint8_t _tx[CH32RV_WIRE_BUFFER_SIZE];
    volatile uint8_t _tx_len = 0;
    uint8_t _rx[CH32RV_WIRE_BUFFER_SIZE];
    volatile uint8_t _rx_len = 0;
    volatile uint8_t _rx_read = 0;
    /* Answering a read in onRequest(). */
    volatile bool _slave_replying = false;
    /* SMBus PEC (setPEC()): 0 off, PEC_ON, and PEC_CARRY while a write ended
     * without a STOP carries its CRC (_pec_crc) into the next transfer of the
     * same transaction. */
    enum : uint8_t { PEC_ON = 1, PEC_CARRY = 2 };
    uint8_t _pec = 0;
    uint8_t _pec_crc = 0;
};
