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
#include "SoftWire.h"
#include "internal/ch32rv_pins.h"
#include "internal/ch32rv_route.h"
#include "pins_arduino.h"
#include "internal/ch32rv_instances.h"

#include <stdint.h>

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
    /* Target at `address` on the bus's pins - a plain member, not a virtual
     * one, in a file of its own (WireTarget.cpp), so that only a sketch
     * calling it links the target's interrupt handling (WireIrq.cpp). */
    void begin(uint8_t address);
    /* Target at `address` on these pins (SDA, SCL); false when they cannot
     * carry this bus. */
    bool begin(uint8_t address, int sda, int scl, uint32_t frequency)
    {
        if (!prepare(sda, scl, frequency)) {
            return false;
        }
        begin(address);
        return true;
    }
    using ::TwoWire::begin;
    void end() override;

    void setClock(uint32_t freq) override;

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

protected:
    uint8_t transmit(bool stop) override;
    size_t receive(uint8_t address, size_t len, bool stop) override;

private:
    void targetStart(uint8_t address);   /* Wire.cpp: begin(address) after begin() */
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

    /* Set when a transfer ended badly, so the next one resets the peripheral
     * first: an aborted transfer can leave BUSY asserted forever. */
    bool _needs_recovery = false;

    /* Target state. The callbacks run in interrupt context; _slave_replying
     * (in TwoWire) lets write() tell "inside onRequest()" apart from "outside
     * any transmission", where write() must go nowhere. */
    void (*_on_receive)(int) = nullptr;
    void (*_on_request)(void) = nullptr;
    /* A write to this slave is in progress: onReceive() is still owed, at
     * STOP or - before onRequest() - at a repeated START that turns to read. */
    volatile bool _slave_receiving = false;

    /* How much of _tx a target has sent the controller. */
    volatile uint8_t _tx_sent = 0;
};

}  // namespace arduino

/* As many names as the board has enabled I2Cs (ch32rv_instances.h): the bare
 * name first and Wire1 second, as elsewhere in the Arduino ecosystem. An I2C
 * set to Software in the board menu makes its name a SoftWire. */
#if CH32RV_WIRE_COUNT > 0
#define WIRE_INTERFACES_COUNT CH32RV_WIRE_COUNT
#if CH32RV_WIRE0_SOFT
extern arduino::SoftWire Wire;            /* WireInstance.cpp */
#else
extern arduino::CH32RVTwoWire Wire;       /* WireInstance.cpp */
#endif
#endif
#if CH32RV_WIRE_COUNT > 1
#if CH32RV_WIRE1_SOFT
extern arduino::SoftWire Wire1;           /* Wire1Instance.cpp */
#else
extern arduino::CH32RVTwoWire Wire1;      /* Wire1Instance.cpp */
#endif
#endif
