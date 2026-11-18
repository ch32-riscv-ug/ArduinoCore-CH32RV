/* The chip's built-in op-amps (OPA), their inputs and output chosen by pin:
 *
 *     CH32RVOpamp opa(1);                  // OPA1
 *     opa.begin(PB15, PB11, PA3);          // +, -, output: pads of OPA1 (CH32RV_OPA_PADS)
 *
 * With no feedback wired it compares + with - (open loop): the output goes to
 * the rail of whichever is higher. Wire the output to - for a follower, or
 * through resistors for a gain. beginPga() uses the internal gain where the
 * chip has one (V4x7: 4, 8, 16, 32). An input pad is left as the sketch set it
 * (an analog input, or a GPIO driving it); the output pad becomes the op-amp's.
 *
 * V003 (one, its output fixed on PD4), V20x / V30x (two / four), V4x7 (one).
 * begin() is false for pads that are not that op-amp's, and on the families
 * whose selection registers this library does not set yet (V00x, V205, L103,
 * H41x, X035, M030: their pads are in CH32RV_OPA_PADS all the same). */
#pragma once

#include <Arduino.h>

class CH32RVOpamp {
public:
    explicit CH32RVOpamp(uint8_t unit = 1) : _unit(unit) {}

    /* outPin -1: the op-amp's first output pad. */
    bool begin(int positivePin, int negativePin, int outPin = -1);
    /* + from positivePin, the gain internal (no - pad): false where there is
     * no programmable gain or for a gain it does not have. */
    bool beginPga(int positivePin, uint8_t gain, int outPin = -1);
    void end();
    bool isBegun() const { return _begun; }

private:
    bool select(int pin, uint8_t role, uint8_t &sel) const;

    const uint8_t _unit;
    bool _begun = false;
};
