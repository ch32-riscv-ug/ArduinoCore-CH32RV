/* The chip's built-in comparators (CMP), their inputs and output chosen by pin:
 *
 *     CH32RVComparator cmp(1);             // CMP1
 *     cmp.begin(PA2, PB1, PB12);           // +, -, output: pads of CMP1 (CH32RV_CMP_PADS)
 *     int above = cmp.read();              // 1: + above -
 *
 * The output pad (-1: none) carries the result; attachInterrupt() on it gives
 * the edges. read() takes the comparator's own status where the chip has one
 * (H41x), else the output pad's level: -1 with neither. An input pad is left
 * as the sketch set it (an analog input, or a GPIO driving it).
 *
 * V007 / M007 (CMP1), V205 (2), L103 / M103 (3), X035 / X033 (3), H41x (1).
 * Inner inputs: OPA_OUTPUT as + (V205: the op-amp of the same number; H41x:
 * OPA1), REFERENCE as - (H41x: 1/4, 2/4 or 3/4 of VDD33A, setReference()).
 * M030's comparators (the motor stage's) are not here. */
#pragma once

#include <Arduino.h>

class CH32RVComparator {
public:
    static constexpr int OPA_OUTPUT = -2;
    static constexpr int REFERENCE = -3;

    explicit CH32RVComparator(uint8_t unit = 1) : _unit(unit) {}

    /* outPin -1: no pad (H41x: read() still works). */
    bool begin(int positivePin, int negativePin, int outPin = -1);
    /* 1 with + above -, 0 below, -1 where there is no way to see it. */
    int read() const;
    /* The nearest the chip has, 0 off: V205 12.5 / 25 / 50 mV, H41x 10 / 20 /
     * 30, V007 24, L103 15, X035 on. False where it has none. */
    bool setHysteresis(uint8_t millivolts);
    /* REFERENCE's level in quarters of the supply (1 .. 3): false where the
     * chip has no such reference. Taken at begin() or at once. */
    bool setReference(uint8_t quarters);
    void end();
    bool isBegun() const { return _begun; }

private:
    bool select(int pin, uint8_t role, uint8_t &sel, uint8_t &af) const;
    void apply();

    const uint8_t _unit;
    bool _begun = false;
    int8_t _out = -1;
    uint8_t _hyst = 0, _ref = 2;
    uint32_t _fields = 0;
};
