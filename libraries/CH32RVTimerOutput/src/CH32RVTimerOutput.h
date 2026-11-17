/* Timer outputs beyond analogWrite(): a complementary pair with dead time
 * and a break input, a one-shot pulse, and output compare.
 *
 *     CH32RVComplementaryPwm bridge;
 *     bridge.begin(PA8, PB13, 20000);          // CH1 and CH1N of TIM1, 20 kHz
 *     bridge.setDeadTime(500);                 // ns, both low around each switch
 *     bridge.attachBreak(PB12);                // low on BKIN: both off at once
 *     bridge.write(128);                       // of 255
 *
 *     CH32RVOnePulse shot;
 *     shot.begin(PA6);
 *     shot.fire(100, 20);                      // 20 us high, 100 us from now
 *
 *     CH32RVOutputCompare oc;
 *     oc.begin(PA0, 1000);                     // a 1000 us period
 *     oc.set(CH32RVOutputCompare::TOGGLE, 250);   // toggles 250 us into each
 *
 * Pins are a timer's pads on any route the chip offers (CH32RVTimer.h
 * ch32rv_timer_pad_find): a pad on several timers takes the first that is
 * free. Each object holds its whole timer through CH32RVTimer.h's lease,
 * politely: begin() is false when analogWrite(), tone(), Servo or another
 * library has it, and an object whose timer is taken over later stops
 * (isAttached() false). end() leaves its pads plain low outputs. A remap moves all of a timer's pads: the timer's
 * other pads on the old route stop being its. */
#pragma once

#include <Arduino.h>
#include <CH32RVCallback.h>

#include "CH32RVTimer.h"

/* A channel's output and its complementary output (CHx and CHxN) of an
 * advanced timer, as a half bridge's high and low side: CHxN is CHx
 * inverted, with a dead time where both are low around each switch, and a
 * break input that turns both off in hardware. Both are active high; on a
 * break (or end()) they go low. */
class CH32RVComplementaryPwm {
public:
    /* pin: CHx, pinN: CHxN of the same channel (and route). frequency in
     * Hz; resolution: the bits write() takes (1 .. 16). */
    bool begin(int pin, int pinN, uint32_t frequency, uint8_t resolution = 8);
    void end();
    bool isAttached() const { return ch32rv_timer_lease_valid(&_lease); }

    /* CHx high for duty / 2^resolution of each period (0: CHx low and CHxN
     * high; 2^resolution: the other way round), less the dead time. */
    bool write(uint32_t duty);
    /* The dead time in ns, the nearest the timer's dead-time field reaches
     * at or above it (in steps of the timer clock, coarser for long ones; the
     * longest is 1008 clocks: 10.5 us at 96 MHz). Returns what was set; 0
     * turns it off. Before or after begin(). */
    uint32_t setDeadTime(uint32_t nanoseconds);
    uint32_t deadTime() const { return _deadNs; }

    /* The timer's break input (BKIN) on `pin`: at activeLevel, the hardware
     * turns both outputs off at once (low) and keeps them off until
     * resume(). mode: the pad's input mode. False when the pin is not this
     * timer's break input on this route. */
    bool attachBreak(int pin, int activeLevel = LOW, uint8_t mode = INPUT_PULLUP);
    void detachBreak();
    /* A break came since the last resume() (it stays latched). */
    bool breakActive();
    /* Outputs back on, if the break input is no longer active. */
    bool resume();

private:
    bool begin(const CH32RVTimerPad &pad, const CH32RVTimerPad &padN, uint32_t frequency, uint8_t resolution);
    void applyBdtr();

    const CH32RVTimerOwner _owner = {this, nullptr, nullptr};   /* the manager keeps a pointer */
    CH32RVTimerLease _lease = {};
    CH32RVTimerPad _pad = {}, _padN = {};
    uint32_t _base = 0;
    uint32_t _steps = 0;            /* counter steps in a period */
    uint32_t _deadNs = 0;
    uint32_t _deadAsk = 0;          /* what setDeadTime() was given */
    uint8_t _dtg = 0;               /* BDTR's dead-time field */
    uint8_t _bits = 8;
    bool _break = false, _breakHigh = false;
};

/* One pulse of a set width after a set delay, by the timer's one-pulse mode:
 * the counter runs once and stops, so the pulse is exact to the timer's tick
 * whatever the CPU does. Started by fire(), or in hardware by an edge on a
 * trigger pin (channel 1 or 2 of the same timer, another channel than the
 * output's). */
class CH32RVOnePulse {
public:
    /* pin: the pulse's pad (a channel of a timer). polarity: HIGH pulses
     * from a low line, LOW the reverse. triggerPin: -1 for fire() only. */
    bool begin(int pin, int polarity = HIGH, int triggerPin = -1, int triggerEdge = RISING);
    void end();
    bool isAttached() const { return ch32rv_timer_lease_valid(&_lease); }

    /* The pulse: delayMicros (at least one tick) after the start, widthMicros
     * long. The tick is the timer clock divided so delay + width fits the
     * counter. False when it does not (over about 65536 x 65536 clocks). */
    bool setPulse(uint32_t delayMicros, uint32_t widthMicros);
    /* Start one now (setPulse() first, or give the times here). False while
     * one is running. */
    bool fire();
    bool fire(uint32_t delayMicros, uint32_t widthMicros) { return setPulse(delayMicros, widthMicros) && fire(); }
    /* A pulse is under way (its delay or the pulse itself). */
    bool busy();

private:
    bool begin(const CH32RVTimerPad &pad, const CH32RVTimerPad *trigger, int polarity, int triggerEdge);

    const CH32RVTimerOwner _owner = {this, nullptr, nullptr};
    CH32RVTimerLease _lease = {};
    uint32_t _base = 0;
    uint8_t _channel = 0;
    uint8_t _pin = 0;
    bool _wide = false;
};

/* A channel in output-compare mode: the counter runs over a set period and,
 * at a set time into each, the pad goes active, inactive or toggles - and a
 * callback can run then. TOGGLE at any time makes a square wave of half the
 * period's frequency. */
class CH32RVOutputCompare {
public:
    enum Mode : uint8_t {
        TIMING = 0,          /* the pad does nothing; only the callback */
        ACTIVE = 1,          /* high at the match */
        INACTIVE = 2,        /* low at the match */
        TOGGLE = 3,          /* the other level at each match */
        FORCE_INACTIVE = 4,  /* low now */
        FORCE_ACTIVE = 5,    /* high now */
    };
    typedef CH32RVCallback<void()> Callback;

    /* pin: a channel of a timer; periodMicros: the counter's period. */
    bool begin(int pin, uint32_t periodMicros);
    void end();
    bool isAttached() const { return ch32rv_timer_lease_valid(&_lease); }

    /* What the pad does `atMicros` into each period (0 .. the period). */
    bool set(Mode mode, uint32_t atMicros);
    /* Called from the timer's interrupt at each match; nullptr for none. */
    void onMatch(Callback callback);
    /* The tick the times are rounded to, in Hz. */
    uint32_t tickHz() const { return _tickHz; }

private:
    bool begin(const CH32RVTimerPad &pad, uint32_t periodMicros);
    static void onInterrupt(void *self);

    const CH32RVTimerOwner _owner = {this, nullptr, nullptr};
    CH32RVTimerLease _lease = {};
    Callback _callback;
    uint32_t _base = 0;
    uint32_t _tickHz = 0;
    uint32_t _steps = 0;
    uint8_t _channel = 0;
    uint8_t _pin = 0;
    bool _wide = false;
};
