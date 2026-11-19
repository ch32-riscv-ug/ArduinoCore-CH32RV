/* ADC1's injected group: up to four channels converted as one round, started
 * by the sketch or by a timer, ahead of (and between) analogRead()'s and the
 * stream's regular conversions - for samples that must be taken at a set
 * instant, such as a motor's currents in the middle of a PWM period.
 *
 *     CH32RVAdcInjected inj;
 *     const pin_size_t pins[] = {PA0, PA1};
 *     inj.begin(pins, 2);
 *     uint16_t v[2];
 *     inj.convert(v);                       // one round now
 *     inj.onRound([](const uint16_t *v, uint8_t n) { ... });
 *     inj.startTimer(1000);                 // a round 1000 times a second
 *
 * Values are the ADC's raw counts (analogReadResolution() does not apply):
 * 0 .. 2^bits() - 1. The timer is one of the chip's injected-group sources
 * (the variant's CH32RV_ADC_JTRIGGERS: a timer's TRGO or a channel's compare
 * event), taken whole through CH32RVTimer.h's lease.
 *
 * simultaneous(pins2, count) after begin() adds ADC2 (dual mode, on the
 * series with CH32RV_ADC_DUAL_INJECTED_SIMULTANEOUS): ADC2 converts pins2[i]
 * at the same moment ADC1 converts pin i, on the same start. A round then has
 * twice the values - ADC1's count, then ADC2's - in convert()'s `out`, in
 * value(), and in the callback's (values, 2 * count). Not together with a
 * dual CH32RVAdcStream (the two set the same mode). */
#pragma once

#include <Arduino.h>
#include <CH32RVCallback.h>

#include "CH32RVTimer.h"

class CH32RVAdcInjected {
public:
    typedef CH32RVCallback<void(const uint16_t *values, uint8_t count)> Callback;

    /* 1 .. 4 ADC pins, converted in this order. False for a pin that is no
     * ADC1 channel. */
    bool begin(const pin_size_t *pins, uint8_t count);
    bool begin(pin_size_t pin) { return begin(&pin, 1); }
    /* ADC2's pins beside ADC1's (dual mode); false without a second ADC, for
     * a pin that is no ADC2 input, or one of ADC1's channel at the same place. */
    bool simultaneous(const pin_size_t *pins2, uint8_t count);
    void end();

    /* One round now (software start), waited for: the values into `out`
     * (count of them). False while a timer runs the rounds. */
    bool convert(uint16_t *out);

    /* Rounds at `hz` from a timer the chip can start the injected group with;
     * false when every such timer is taken or hz is out of its reach. */
    bool startTimer(uint32_t hz);
    void stop();
    /* The rate the timer gives (the nearest to what was asked). */
    uint32_t rate() const { return _rate; }

    /* Called from the ADC's interrupt after each round; nullptr: none. */
    void onRound(Callback callback);
    /* The last round's value of the i-th pin (after simultaneous(): ADC1's
     * 0 .. count - 1, then ADC2's), and the rounds so far. */
    uint16_t value(uint8_t i) const;
    uint32_t rounds() const { return _rounds; }
    uint8_t bits() const;

    /* The ADC's interrupt (CH32RVAdcInjected.cpp). */
    void service();

private:
    const CH32RVTimerOwner _owner = {this, nullptr, nullptr};
    CH32RVTimerLease _lease = {};
    uint8_t _channels[4] = {};
    uint8_t _count = 0;
    bool _dual = false;
    bool _begun = false;
    uint32_t _rate = 0;
    volatile uint32_t _rounds = 0;
    volatile uint16_t _values[8] = {};
    Callback _callback;
};
