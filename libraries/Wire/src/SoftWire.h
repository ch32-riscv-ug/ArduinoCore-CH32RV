/* Bit-banged I2C controller for CH32 RISC-V.
 *
 * The same API as <Wire.h>, on any two pads. The hardware I2C can only reach
 * the pads its routes name, and on the small parts those are often gone -
 * not bonded out on your package, or already carrying something else.
 *
 * It derives from TwoWire (<TwoWire.h>), so anything written against
 * `TwoWire *` / `TwoWire &` takes one of these unchanged:
 *
 *   SoftWire bus(PA1, PA2);        // SDA, SCL
 *   Adafruit_Something dev(&bus);
 *
 * The pins can also be named later, as on <Wire.h>: bus.begin(sda, scl) or
 * bus.setPins(sda, scl). Unlike the hardware bus, any two pads will do.
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

#include "TwoWire.h"
#include "internal/ch32rv_pins.h"

#include <stdint.h>

namespace arduino {

class SoftWire : public ::TwoWire {
public:
    SoftWire(uint8_t sda, uint8_t scl) : _sda(sda), _scl(scl) {}

    using ::TwoWire::begin;

    void begin() override;
    /* Target mode is not possible here - see the note at the top. Accepted so
     * that code written for another core still compiles; the bus stays a
     * controller and nothing answers at `address`. */
    void begin(uint8_t address) override { (void)address; begin(); }
    /* Any two distinct pads; an open bus moves to them. */
    bool setPins(int sda, int scl) override;
    void end() override;

    /* Sets the floor on the half period, not an achieved frequency: a
     * bit-banged bus cannot hit a number. Anything at or above what the loop
     * already produces leaves it running flat out. getClock() returns what
     * was asked. */
    void setClock(uint32_t freq) override;

    /* Free a bus a target is holding, as Wire's clearBus() does: while SDA
     * reads low, up to 9 SCL pulses (a byte and its ACK), then a STOP. true
     * when both lines read high afterwards. Explicit, never automatic: the
     * pulses reach every device on the bus. Works on an open or closed bus;
     * a stretching target is waited for, up to the timeout. */
    bool clearBus(void);

    /* The hardware I2C this bus is bound to: none, as Wire's says while closed. */
    uint8_t peripheral(void) const { return 0; }

    /* No target mode, so no callbacks can ever fire. */
    void onReceive(void (*)(int)) override {}
    void onRequest(void (*)(void)) override {}

protected:
    uint8_t transmit(bool stop) override;
    size_t receive(uint8_t address, size_t len, bool stop) override;

private:
    void hold(uint8_t pin);            /* drive low                     */
    void release(uint8_t pin);         /* let the pull-up take it high  */
    bool scl_high(void);               /* release SCL, honour stretching */
    void half(void);

    void start_condition(void);
    void stop_condition(void);
    bool write_byte(uint8_t b);        /* true when the target ACKed    */
    uint8_t read_byte(bool ack);

    uint8_t _sda;
    uint8_t _scl;

    uint16_t _half_us = 5;             /* ~100 kHz if the loop were free */
};

}  // namespace arduino

using arduino::SoftWire;
