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
 * A target too: bus.begin(0x42) answers at 0x42 (or a 10-bit address with
 * WIRE_10BIT, and the general call with setGeneralCall(true)) with Wire's contract
 * (onReceive() after the STOP or a repeated START, onRequest() at the address,
 * 0xFF past the reply, the head of a long write). A START (SDA falling,
 * attachInterrupt) starts one interrupt that follows the controller's clock by
 * watching both lines until the STOP: SDA is sampled as SCL rises, and what the
 * next low phase does to SDA (the ACK, the next bit of a reply) is decided in
 * the high phase before it and written as soon as SCL falls. Where a callback
 * runs it holds SCL low until it returns (clock stretching), so the callbacks
 * may take their time. The bit loop runs from RAM (about 0.5 KB); a CH32X035
 * at 48 MHz and a CH32V003 at 24 MHz keep up with a 100 kHz controller. The
 * CPU stays in that interrupt while the bus is busy, other targets' transfers
 * included (README). SDA's EXTI line is the target's while it runs; SCL takes
 * no interrupt. A SoftWire is a controller or a target, one at a time: a
 * transfer started while it is a target returns 4 / 0 (its own START would
 * be followed by its own interrupt).
 */
#pragma once

#include "TwoWire.h"
#include "internal/ch32rv_pins.h"

#include <stdint.h>

namespace arduino {

class SoftWire;
/* Wire10Bit.cpp: a 10-bit address after the START (weak: see WIRE_10BIT). */
bool softwire_send10(SoftWire &bus, uint16_t address, bool read) __attribute__((weak));
/* SoftWireTargetExt.cpp: the extended target's transfer (weak: see above). */
uint8_t softwire_follow_ext(SoftWire &bus) __attribute__((weak));

class SoftWire : public ::TwoWire {
public:
    SoftWire(uint8_t sda, uint8_t scl) : _sda(sda), _scl(scl) {}

    using ::TwoWire::begin;

    void begin() override;
    /* Target at `address` (7 bits, or 10 with WIRE_10BIT) on the bus's pins -
     * a plain member in a file of its own (SoftWireTarget.cpp), as Wire's, so
     * only a sketch that calls it links the target and attachInterrupt(). */
    void begin(uint16_t address);
    /* Target at `address` on these pins (SDA, SCL); false when they cannot
     * carry it. */
    bool begin(uint16_t address, int sda, int scl, uint32_t frequency)
    {
        if (!prepare(sda, scl, frequency)) {
            return false;
        }
        begin(address);
        return _slave;
    }
    /* Any two distinct pads; an open bus moves to them. */
    bool setPins(int sda, int scl) override;
    void end() override;

    /* Sets the floor on the half period, not an achieved frequency: a
     * bit-banged bus cannot hit a number. Anything at or above what the loop
     * already produces leaves it running flat out. getClock() returns what
     * was asked. */
    bool setClock(uint32_t freq) override;

    /* Free a bus a target is holding, as Wire's clearBus() does: while SDA
     * reads low, up to 9 SCL pulses (a byte and its ACK), then a STOP. true
     * when both lines read high afterwards. Explicit, never automatic: the
     * pulses reach every device on the bus. Works on an open or closed bus;
     * a stretching target is waited for, up to the timeout. */
    bool clearBus(void);

    /* The hardware I2C this bus is bound to: none, as Wire's says while closed. */
    uint8_t peripheral(void) const { return 0; }

    /* A target's callbacks, in interrupt context with SCL held low (stretched)
     * where the controller would otherwise move on: onReceive(count) after the
     * STOP (or at a repeated START, before the next address), onRequest() at
     * a read's address, where what it write()s is what the controller reads. */
    void onReceive(void (*callback)(int)) override { _on_receive = callback; }
    void onRequest(void (*callback)(void)) override { _on_request = callback; }
    /* Answer a general call (address 0) as a target too, as Wire's
     * (SoftWireTargetExt.cpp). */
    void setGeneralCall(bool enable);
    bool getGeneralCall(void) const { return _general_call; }

    /* The target's START interrupt (SoftWireTarget.cpp). Public for the
     * reason Wire's ev_irq() is; not part of the sketch-facing API. */
    void startEdge(void);

protected:
    uint8_t transmit(bool stop) override;
    size_t receive(uint16_t address, size_t len, bool stop) override;

private:
    friend bool softwire_send10(SoftWire &bus, uint16_t address, bool read);
    void hold(uint8_t pin);            /* drive low                     */
    void release(uint8_t pin);         /* let the pull-up take it high  */
    bool scl_high(void);               /* release SCL, honour stretching */
    void half(void);

    void start_condition(void);
    void stop_condition(void);
    bool write_byte(uint8_t b);        /* true when the target ACKed    */
    bool send_address(uint16_t address, bool read);   /* after the START */
    uint8_t read_byte(bool ack);

    uint8_t _sda;
    uint8_t _scl;

    /* Half a clock period in SysTick ticks, and the tick the next half ends
     * at: each half waits for a deadline, not for a fixed delay after the pin
     * writes, so their cost and the time SCL takes to rise come out of the
     * wait and the clock averages what was asked (a plain delay of the half
     * ran a V205's 100 kHz at 27 kHz). Ticks, not micros(): a microsecond's
     * grain made halves near 1 us wait up to a whole one more. A half that
     * overran is not made up by a shorter one: never faster than asked. */
    uint32_t _half_ticks = 1;
    uint32_t _due_ticks = 0;

    /* The target (SoftWireTarget.cpp). */
    void (*_on_receive)(int) = nullptr;
    void (*_on_request)(void) = nullptr;
    uint8_t _own = 0;                  /* its address (7 bits)                */
    volatile uint8_t _tx_sent = 0;     /* how much of _tx the controller read */
    /* The transfer being followed, inside the START's interrupt: the bit
     * loop (in RAM) and the byte's edges and callbacks (in flash) share it. */
    uint8_t _t_mode = 0;               /* address, receiving, sending, other  */
    uint8_t _t_bits = 0;               /* clocks of this byte's frame, 0-9    */
    uint8_t _t_in = 0;                 /* the byte coming in                  */
    uint8_t _t_out = 0;                /* the byte going out                  */
    uint8_t _t_act = 0;                /* what the next low phase does        */
    bool _t_owed = false;              /* written to: onReceive() is owed     */
    volatile bool _t_free = false;     /* both lines last read high (no transfer) */
    uint8_t follow(void);              /* one transfer, START to STOP          */
    void byteEdge(bool sampled);       /* the 8th and 9th clocks' decisions    */
    void lowWork(void);                /* a low phase with a callback          */
    static void targetStop(SoftWire *bus);   /* end()'s half for a target */

    /* The extended target - a 10-bit address, the general call - in
     * SoftWireTargetExt.cpp: a bit loop of its own, chosen at begin(address),
     * so a plain 7-bit target runs the loop it always had (a V003 at 24 MHz
     * missed ACKs at 100 kHz with both in one, 2026-11-17). Linked only by a
     * sketch naming WIRE_10BIT or calling setGeneralCall(); softwire_ext is
     * the weak way in. */
    friend uint8_t softwire_follow_ext(SoftWire &bus);
    uint8_t followExt(void);
    void byteEdgeExt(bool sampled);
    uint8_t acceptWrite(void);         /* a write to it: ACK, onReceive() owed */
    uint16_t _own10 = 0;               /* its 10-bit address                  */
    bool _general_call = false;        /* address 0 answered too              */
    bool _t_ext = false;               /* the extended loop follows           */
    bool _t_ten = false;               /* its address is 10-bit               */
    bool _t_10 = false;                /* its 10-bit address matched since the START */
    uint8_t _t_aw = 0;                 /* its address byte with W             */
};

}  // namespace arduino

using arduino::SoftWire;
