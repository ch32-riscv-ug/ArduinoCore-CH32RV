/* Wire (I2C) for CH32 RISC-V.
 *
 * Controller mode, polled. A sketch names the pins the way it does on an ESP32,
 * Wire.begin(sda, scl[, frequency]); Wire.begin() with no arguments, as on AVR,
 * is the fallback onto the board's default pins (ch32rv_instances.h). The type
 * libraries take, TwoWire, is the base in <TwoWire.h>.
 *
 * Target mode (begin(address), onReceive, onRequest) is interrupt-driven -
 * a target cannot poll, because the controller decides when to talk. The AVR
 * semantics are kept where libraries depend on them:
 *
 *   - onReceive(count) runs in interrupt context, after the stop condition,
 *     with the message readable through available()/read()
 *   - onRequest() runs in interrupt context at the address match; what it
 *     write()s is what the controller reads
 *   - a controller that reads more than onRequest() provided gets 0xFF, the
 *     bus's idle level, rather than a repeat of the last byte
 *   - a message longer than CH32RV_WIRE_BUFFER_SIZE keeps its head and drops
 *     the tail, as AVR's twi does
 *
 * A target is a controller as well: beginTransmission() / requestFrom() on a
 * bus begun with begin(address) wait for the bus to be free (a transfer to
 * this target goes on being answered meanwhile), take it, and go back to
 * listening. The address may be 10-bit (WIRE_10BIT, <TwoWire.h>), and
 * setGeneralCall(true) answers address 0 as well.
 *
 * Multi-master: a controller that loses arbitration (or sees another
 * controller's START or STOP in the middle of its transfer) lets the bus go,
 * waits for it to be free and starts the transfer again, up to
 * CH32RV_WIRE_ARBITRATION_RETRIES more times; then endTransmission() returns
 * 4 and requestFrom() 0. AVR, arduino-esp32 and STM32duino report a lost
 * arbitration as 4 and leave the next try to the sketch; the bus being
 * someone else's for a moment is the normal case on a multi-controller bus,
 * so it is retried here.
 *
 * Every wait in the controller driver is bounded. A missing pull-up or a device
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

/* How many more times a controller transfer that lost the bus to another
 * controller is started again before endTransmission() gives 4. */
#ifndef CH32RV_WIRE_ARBITRATION_RETRIES
#define CH32RV_WIRE_ARBITRATION_RETRIES 3
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
class CH32RVTwoWire;
/* Wire10Bit.cpp: a 10-bit address after the START (weak: see WIRE_10BIT). */
bool wire_start10(CH32RVTwoWire &bus, uint16_t address, bool read) __attribute__((weak));

class CH32RVTwoWire : public ::TwoWire {
public:
    explicit CH32RVTwoWire(uint8_t number) : _number(number) {}

    void begin() override;
    /* Target at `address` (7 bits, or 10 with WIRE_10BIT) on the bus's pins,
     * and a controller still - a plain member, not a virtual one, in a file
     * of its own (WireTarget.cpp), so that only a sketch calling it links the
     * target's interrupt handling (WireIrq.cpp). */
    void begin(uint16_t address);
    /* Target at `address` on these pins (SDA, SCL); false when they cannot
     * carry this bus. */
    bool begin(uint16_t address, int sda, int scl, uint32_t frequency)
    {
        if (!prepare(sda, scl, frequency)) {
            return false;
        }
        begin(address);
        return true;
    }
    using ::TwoWire::begin;
    void end() override;

    bool setClock(uint32_t freq) override;

    void onReceive(void (*)(int)) override;
    void onRequest(void (*)(void)) override;
    /* Answer a general call (address 0, a write to every target) as a target
     * too: its bytes come to onReceive() like any write. Off by default; set
     * before or after begin(address). (WireTarget.cpp) */
    void setGeneralCall(bool enable);
    bool getGeneralCall(void) const { return _general_call; }

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

    /* Free a bus that a target is holding: the classic hang where a slave was
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
    size_t receive(uint16_t address, size_t len, bool stop) override;

private:
    friend bool wire_start10(CH32RVTwoWire &bus, uint16_t address, bool read);
    void targetStart(uint16_t address);  /* Wire.cpp: begin(address) after begin() */
    /* The target's registers and its share of a controller transfer, in
     * WireTarget.cpp. Weak references from Wire.cpp (the library is an
     * archive, and a weak reference does not pull a member out of one): only
     * a sketch calling begin(address), which is in that file, links them, and
     * nothing calls them while _slave is false. */
    void targetRegs(void) __attribute__((weak));     /* own address, general call, ACK */
    bool targetClaim(void) __attribute__((weak));    /* wait for the bus, the interrupts off */
    void targetRelease(void) __attribute__((weak));  /* the interrupts back on */

    bool wait_flag1(uint16_t mask, bool set);
    bool claim(void);
    void release_target(void);
    bool start(uint16_t address, bool read);
    uint8_t fail(uint8_t nack_code);
    uint8_t transmit_once(bool stop);
    int receive_once(uint16_t address, size_t len, bool stop);
    bool bus_stale(void) const;
    void stop(void);
    void reset(void);
    void recover(void);
    void release_bus(void);
    void apply_clock(void);

    const uint8_t _number;
    /* The fields below start at zero because a bus is a global object, in
     * .bss: no initialiser, so the constructor does not store the zeros again.
     * 0 = closed; otherwise 1 + index into the I2C table. (Ordered by size,
     * the bytes after the words: they fill what the base leaves.) */
    uint8_t _slot;
    /* Route chosen by setPins() / setRoute() for the next begin(), already
     * looked up, so begin() never searches. nullptr = the default. */
    uint8_t _want_index;
    const ch32rv_route_t *_want;
    /* Of the bound I2C; 0 while closed. */
    uint32_t _base;

    /* Target state. The callbacks run in interrupt context; _slave_replying
     * (in TwoWire) lets write() tell "inside onRequest()" apart from "outside
     * any transmission", where write() must go nowhere. */
    void (*_on_receive)(int) = nullptr;
    void (*_on_request)(void) = nullptr;

    uint8_t _scl_pin;
    uint8_t _sda_pin;
    /* Set when a transfer ended badly, so the next one resets the peripheral
     * first: an aborted transfer can leave BUSY asserted forever. */
    bool _needs_recovery = false;
    /* The last transfer ended without a STOP (sendStop false): the bus is
     * ours and BUSY is meant, so bus_stale() is not asked. */
    bool _held = false;
    /* A read ended with sendStop false armed the repeated START (end_read):
     * the next start() only waits for SB. Setting START again there is a
     * read-modify-write of CTLR1 racing the peripheral, which clears START
     * when it sends the armed one - writing the old 1 back asks for a second
     * START in the middle of the next address. */
    bool _restart_armed = false;
    /* A write to this slave is in progress: onReceive() is still owed, at
     * STOP or - before onRequest() - at a repeated START that turns to read. */
    volatile bool _slave_receiving = false;
    /* The general call (address 0) is answered as a target. */
    bool _general_call = false;
    /* How much of _tx a target has sent the controller. */
    volatile uint8_t _tx_sent = 0;
    /* The target's own address (WIRE_10BIT), kept for a reset of the
     * peripheral (recovery, clearBus()). */
    uint16_t _own = 0;
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
