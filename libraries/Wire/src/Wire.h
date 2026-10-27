/* Wire (I2C) for CH32 RISC-V.
 *
 * Master mode, polled. A sketch names the pins the way it does on an ESP32,
 * Wire.begin(sda, scl[, frequency]); Wire.begin() with no arguments, as on AVR,
 * is the fallback onto the board's default pins (ch32rv_instances.h). The type
 * libraries take, TwoWire, is the base in <TwoWire.h>.
 *
 * Slave mode (begin(address), onReceive, onRequest) is interrupt-driven -
 * a slave cannot poll, because the master decides when to talk. The AVR
 * semantics are kept where libraries depend on them:
 *
 *   - onReceive(count) runs in interrupt context, after the stop condition,
 *     with the message readable through available()/read()
 *   - onRequest() runs in interrupt context at the address match; what it
 *     write()s is what the master reads
 *   - a master that reads more than onRequest() provided gets 0xFF, the
 *     bus's idle level, rather than a repeat of the last byte
 *   - a message longer than CH32RV_WIRE_BUFFER_SIZE keeps its head and drops
 *     the tail, as AVR's twi does
 *
 * One instance is a master or a slave, not both: begin() and begin(address)
 * choose, end() switches. Multi-master arbitration is not supported. General
 * call (address 0) is not answered.
 *
 * Every wait in the master driver is bounded. A missing pull-up or a device
 * holding SDA low is the normal I2C failure, and an unbounded wait would turn
 * that into a hung sketch instead of an endTransmission() error code.
 */
#pragma once

#include "TwoWire.h"
#include "ch32rv_pins.h"
#include "ch32rv_route.h"
#include "pins_arduino.h"
#include "ch32rv_instances.h"

#include <stdint.h>

/* AVR's Wire uses 32, and libraries written against it assume a write of more
 * than 32 bytes is truncated rather than sent. Raise it per sketch with
 *   -DCH32RV_WIRE_BUFFER_SIZE=128
 * remembering that both buffers grow, on every Wire instance. */
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

/* clearBus(): how long each SCL pulse waits for a slave that stretches the
 * clock before giving up on it, in microseconds. */
#ifndef CH32RV_WIRE_CLEAR_STRETCH_US
#define CH32RV_WIRE_CLEAR_STRETCH_US 1000UL
#endif

namespace arduino {

/* One object is one bus. Which I2C it drives is decided in begin(), as for
 * HardwareSerial (ADR draft 0015): the object holds state only, each I2C's
 * constants sit in a const table in Wire.cpp, and an I2C set to Disable for
 * the board (CH32RV_I2Cn_MODE = 0) is not in that table.
 *
 * `number` is the name's position (Wire = 0, Wire1 = 1), which is also its
 * row in the table of enabled I2Cs: begin(sda, scl) takes any free I2C those
 * pins are a route of, this row first; begin() without pins takes this row on
 * its default pins (ch32rv_instances.h). */
class CH32RVTwoWire : public ::TwoWire {
public:
    explicit CH32RVTwoWire(uint8_t number) : _number(number) {}

    void begin() override;
    void begin(uint8_t address) override;
    bool begin(int sda, int scl, uint32_t frequency = 0) override;
    bool begin(uint8_t address, int sda, int scl, uint32_t frequency) override;
    void end() override;

    void setClock(uint32_t freq) override;
    uint32_t getClock(void) override { return _clock_hz; }

    /* AVR's timeout API (Wire.h since 1.8.x), with two documented differences.
     *
     * The timeout is ON by default here, at CH32RV_WIRE_TIMEOUT_US (25 ms, the
     * same number AVR's own default uses). On AVR it is off until a sketch
     * asks for it, and a stuck bus hangs the sketch forever; that is not a
     * behaviour worth reproducing, so the default stays on. Pass 0 to turn it
     * off and get AVR's original behaviour.
     *
     * reset_with_timeout is accepted and the bus is reset either way: this
     * driver already marks the peripheral for recovery whenever a wait ends in
     * anything but a NACK, and the next endTransmission()/requestFrom() runs
     * it. Leaving a latched-BUSY peripheral alone has no upside, so `false`
     * does not switch that off. */
    void setWireTimeout(uint32_t timeout = CH32RV_WIRE_TIMEOUT_US,
                        bool reset_with_timeout = false) override;
    /* Sticky: set by any wait that ran out, and only cleared by the call
     * below, so a sketch can check once after a burst of transfers. */
    bool getWireTimeoutFlag(void) override;
    void clearWireTimeoutFlag(void) override;

    void beginTransmission(uint8_t address) override;
    uint8_t endTransmission(bool stopBit) override;
    uint8_t endTransmission(void) override { return endTransmission(true); }

    size_t requestFrom(uint8_t address, size_t len, bool stopBit) override;
    size_t requestFrom(uint8_t address, size_t len) override {
        return requestFrom(address, len, true);
    }

    void onReceive(void (*)(int)) override;
    void onRequest(void (*)(void)) override;

    /* Interrupt entry points, called by the I2Cn_EV/I2Cn_ER vectors in
     * Wire.cpp. Public for the same reason HardwareSerial::irq() is: the
     * vector is a free function and a friend declaration would only dress
     * that up. Not part of the sketch-facing API. */
    void ev_irq(void);
    void er_irq(void);

    /* Move this bus onto another of its pin routes.
     *
     * false, and nothing changed, when the route does not exist on this
     * series - and setPins() (SDA, SCL, as on an ESP32) also refuses an SDA
     * and an SCL that belong to different routes, which the hardware cannot
     * do. Several X035 routes swap the two signals over the same pair of
     * pads, so the order matters and is checked.
     *
     * Calling either after begin() reopens the bus on the new pins and hands
     * the old pads back as inputs. */
    bool setRoute(uint8_t route);
    bool setPins(int sda, int scl) override;

    /* The I2C number this bus is bound to, 0 while closed. */
    uint8_t peripheral(void) const;

    /* Free a bus that a slave is holding: the classic hang where a slave was
     * cut off mid-byte (a reset, a lost clock) and keeps driving SDA low, so
     * every transfer fails and no amount of re-initialising this peripheral
     * helps - the SDA line is not ours. Clocks SCL until the slave lets go
     * (up to 9 pulses, a whole byte plus its ACK), then puts a STOP on the
     * bus. Returns true when both lines read high afterwards.
     *
     * Explicit, never automatic: the pulses reach every device on the bus, and
     * only the sketch knows whether that is safe. Works before or after
     * begin(); a bus that was open is reopened as it was (master, or slave at
     * its own address). Lines are released with the internal pull-up and SCL
     * is never driven high, so a slave stretching the clock is waited for, up
     * to CH32RV_WIRE_CLEAR_STRETCH_US per pulse. */
    bool clearBus(void);

    /* Print/Stream */
    size_t write(uint8_t data) override;
    size_t write(const uint8_t *data, size_t len) override;
    using ::TwoWire::write;
    int available(void) override;
    int read(void) override;
    int peek(void) override;
    void flush(void) override {}

protected:
    uint32_t wireTimeoutUs(void) override { return _timeout_us; }

private:
    bool wait_flag1(uint16_t mask, bool set);
    bool start(uint8_t address, bool read);
    void stop(void);
    void recover(void);

    const uint8_t _number;
    /* The fields below start at zero because a bus is a global object, in
     * .bss: no initialiser, so the constructor does not store the zeros again.
     * 0 = closed; otherwise 1 + index into the I2C table. */
    uint8_t _slot;
    /* Route chosen by setPins() / setRoute() for the next begin(), already
     * looked up, so begin() never searches. nullptr = the default. */
    uint8_t _want_index;
    const ch32rv_route_t *_want;
    /* Of the bound I2C; 0 while closed. */
    uint32_t _base;
    uint8_t _scl_pin;
    uint8_t _sda_pin;

    uint32_t _clock_hz = 100000;
    uint8_t _address = 0;          /* target of the transmission being built */
    bool _started = false;
    bool _transmitting = false;
    bool _tx_overflow = false;
    /* Set when a transfer ended badly, so the next one resets the peripheral
     * first: an aborted transfer can leave BUSY asserted forever. */
    bool _needs_recovery = false;
    /* 0 disables the timeout, as on AVR. */
    uint32_t _timeout_us = CH32RV_WIRE_TIMEOUT_US;
    bool _timeout_flag = false;

    /* Slave state. The callbacks run in interrupt context. _slave_replying
     * is what lets write() tell "inside onRequest()" apart from "outside any
     * transmission", where write() must go nowhere. */
    void (*_on_receive)(int) = nullptr;
    void (*_on_request)(void) = nullptr;
    volatile bool _slave = false;
    volatile bool _slave_replying = false;
    /* A write to this slave is in progress: onReceive() is still owed, at
     * STOP or - before onRequest() - at a repeated START that turns to read. */
    volatile bool _slave_receiving = false;

    /* Shared by the polled master and the slave interrupt handler - never
     * both at once, since the modes are exclusive. Volatile for the halves
     * the handler writes while the sketch reads. */
    uint8_t _tx[CH32RV_WIRE_BUFFER_SIZE];
    volatile uint8_t _tx_len = 0;
    volatile uint8_t _tx_sent = 0;
    uint8_t _rx[CH32RV_WIRE_BUFFER_SIZE];
    volatile uint8_t _rx_len = 0;
    volatile uint8_t _rx_read = 0;
};

}  // namespace arduino

/* As many names as the board has enabled I2Cs (ch32rv_instances.h): the bare
 * name first and Wire1 second, as elsewhere in the Arduino ecosystem. */
#if CH32RV_WIRE_COUNT > 0
#define WIRE_INTERFACES_COUNT CH32RV_WIRE_COUNT
extern arduino::CH32RVTwoWire Wire;       /* WireInstance.cpp */
#endif
#if CH32RV_WIRE_COUNT > 1
extern arduino::CH32RVTwoWire Wire1;      /* Wire1Instance.cpp */
#endif
