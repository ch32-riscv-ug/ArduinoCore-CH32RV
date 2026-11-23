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
#include "internal/ch32rv_dma.h"

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
     * high; 2^resolution: the other way round), less the dead time. Both stay
     * low from begin() to the first write(), which starts them on its duty. */
    bool write(uint32_t duty);
    /* The dead time in ns, the nearest the timer's dead-time field reaches
     * at or above it (in steps of the timer clock, coarser for long ones; the
     * longest is 1008 clocks: 10.5 us at 96 MHz). Returns what was set; 0
     * turns it off. Before or after begin(). */
    uint32_t setDeadTime(uint32_t nanoseconds);
    uint32_t deadTime() const { return _deadNs; }
    /* Asymmetric dead time: risingNs before CHx turns on (OCxREF's rising
     * edge), fallingNs before CHxN turns on (its falling edge). The longer is
     * the timer's dead time (setDeadTime()'s), the shorter its second dead
     * time in that one's steps (at least one step, at most one step less).
     * False where the timer has no second dead time (CH32RV_TIM_DT2_OFFSET:
     * H41x, M030's TIM1) unless the two
     * are equal, which is setDeadTime(). deadTime() / deadTimeShort() then
     * tell what was set. Before or after begin(). */
    bool setDeadTime(uint32_t risingNs, uint32_t fallingNs);
    uint32_t deadTimeShort() const { return _dead2Ns; }

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
    uint32_t _dead2Ask = 0;         /* the shorter of setDeadTime(rising, falling) */
    uint32_t _dead2Ns = 0;
    uint8_t _dt2Edge = 0;           /* 0: none, 1: on the rising edge, 2: on the falling edge */
    uint8_t _dtg = 0;               /* BDTR's dead-time field */
    bool _live = false;             /* write() since begin(): MOE on */
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

/* Several PWM outputs of one timer on one period: the channels (CHx) and,
 * on an advanced timer, their complementary outputs (CHxN, CHx inverted),
 * edge-aligned or center-aligned (the counter runs up and down, so every
 * channel's pulse is centred on the same instant: motor drives, less ripple).
 *
 *     CH32RVPwmTimer pwm;
 *     pwm.begin(PA8, 20000, 10, CH32RVPwmTimer::CENTER);   // TIM1, 20 kHz, 10 bits
 *     pwm.attach(PA9);                                     // TIM1 CH2 too
 *     pwm.write(PA8, 256);
 *     pwm.write(PA9, 768); */
class CH32RVPwmTimer {
public:
    enum Align : uint8_t { EDGE = 0, CENTER = 1 };

    /* pin: a channel pad (CHx or CHxN) of a timer; it picks the timer and the
     * route - the first of the pad's timers free, or `timer` (1, 2, ...) where
     * the pad reaches several and the sketch wants that one. frequency in Hz;
     * resolution: the bits write() takes (1 .. 16). */
    bool begin(int pin, uint32_t frequency, uint8_t resolution = 8, Align align = EDGE, uint8_t timer = 0);
    /* Another channel pad of the same timer and route (at duty 0). */
    bool attach(int pin);
    void end();
    bool isAttached() const { return ch32rv_timer_lease_valid(&_lease); }

    /* duty / 2^resolution of each period high on pin - on a CHxN pad too
     * (its CCxNE alone, not CCxE: the output follows the channel's reference,
     * TIM1 / TIM8 on H417, 2026-11-23). */
    bool write(int pin, uint32_t duty);
    uint8_t timer() const { return _timer; }

    /* This timer starts when `master` does (master's trigger output on its
     * counter's enable, this one in trigger mode): the two run locked, their
     * periods starting together. Both counters restart from 0 now: a PWM
     * already running is cut where it is (call it before write() for none).
     * False where the chip joins the two timers by no internal trigger. */
    bool startWith(CH32RVPwmTimer &master);
    /* The counter stops and waits for an edge (RISING or FALLING) on `pin`,
     * this timer's channel 1 or 2 pad (not one of its outputs), then runs
     * from 0: the outputs start in step with an outside event. */
    bool startOn(int pin, int edge = RISING);

private:
    bool begin(const CH32RVTimerPad &pad, uint32_t frequency, uint8_t resolution, Align align);
    bool connect(const CH32RVTimerPad &pad, bool pin = true);
    void restartCounter();
    const CH32RVTimerPad *find(int pin) const;

    const CH32RVTimerOwner _owner = {this, nullptr, nullptr};
    CH32RVTimerLease _lease = {};
    CH32RVTimerPad _pads[8] = {};
    uint8_t _count = 0;
    uint8_t _timer = 0;
    uint32_t _base = 0;
    uint32_t _steps = 0;
    uint8_t _bits = 8;
    bool _wide = false, _advanced = false;
};

/* Square waves (50 % duty) of one frequency on a timer's channels, each at
 * its own phase: two- or four-phase clocks, interleaved converters. The
 * channels toggle at their compare, the counter running at twice the
 * frequency; a phase from 180 degrees on is the inverse of the one 180 less.
 *
 *     CH32RVPhaseShift clocks;
 *     clocks.begin(PA8, 100000);        // TIM1 CH1, 100 kHz, phase 0
 *     clocks.attach(PA9, 90);           // CH2 a quarter period later */
class CH32RVPhaseShift {
public:
    bool begin(int pin, uint32_t frequency, uint16_t degrees = 0);
    bool attach(int pin, uint16_t degrees);
    /* The outputs restart together (a glitch of up to a period). */
    bool setPhase(int pin, uint16_t degrees);
    void end();
    bool isAttached() const { return ch32rv_timer_lease_valid(&_lease); }

private:
    bool begin(const CH32RVTimerPad &pad, uint32_t frequency, uint16_t degrees);
    void restart();

    const CH32RVTimerOwner _owner = {this, nullptr, nullptr};
    CH32RVTimerLease _lease = {};
    CH32RVTimerPad _pads[4] = {};
    uint16_t _degrees[4] = {};
    uint8_t _count = 0;
    uint32_t _base = 0;
    uint32_t _steps = 0;
    bool _wide = false, _advanced = false;
};

/* Six-step (trapezoidal) drive of a three-phase bridge on an advanced timer:
 * U, V, W on CH1, CH2, CH3 (high sides) and CH1N, CH2N, CH3N (low sides).
 * In each step one phase's high side runs the PWM, another's low side is on
 * and the third is off (both low); step() changes all of them at once (the
 * timer's commutation event).
 *
 *     CH32RVSixStep motor;
 *     motor.begin(PA8, PB13, PA9, PB14, PA10, PB15, 20000);
 *     motor.write(100);                   // of 255
 *     motor.step(0);                      // U PWM, V low; then 1 .. 5 in turn
 *
 *   step  0      1      2      3      4      5
 *   PWM   U      U      V      V      W      W
 *   low   V      W      W      U      U      V */
class CH32RVSixStep {
public:
    bool begin(int uHigh, int uLow, int vHigh, int vLow, int wHigh, int wLow, uint32_t frequency,
               uint8_t resolution = 8);
    void end();
    bool isAttached() const { return ch32rv_timer_lease_valid(&_lease); }

    /* The high side's duty, duty / 2^resolution. */
    bool write(uint32_t duty);
    /* Step 0 .. 5 now; any other value turns all six off. */
    bool step(uint8_t n);
    uint8_t currentStep() const { return _step; }
    /* As CH32RVComplementaryPwm's (between a phase's high and low side). */
    uint32_t setDeadTime(uint32_t nanoseconds);

private:
    const CH32RVTimerOwner _owner = {this, nullptr, nullptr};
    CH32RVTimerLease _lease = {};
    CH32RVTimerPad _pads[6] = {};
    uint32_t _base = 0;
    uint32_t _steps = 0;
    uint8_t _bits = 8;
    uint8_t _dtg = 0;
    uint8_t _step = 0xFF;
};

/* A PWM whose duty changes every period from an array, moved by DMA on the
 * timer's update - a waveform the CPU does not keep up with: a WS2812 chain at
 * 800 kHz (each bit one period, its high time the code), a stepped or smooth
 * duty ramp, servo frames. The CPU only starts it.
 *
 *     CH32RVPwmSequence seq;
 *     seq.begin(PA0, 800000);                 // a timer channel's pad, 800 kHz periods
 *     uint16_t t0 = seq.top() * 32 / 100, t1 = seq.top() * 64 / 100;
 *     ... fill duties[] with t0 / t1 per bit, then a few 0s ...
 *     seq.play(duties, n);                    // returns at once; busy() until done
 *
 * Duties are compare values in timer steps, 0 .. top() (top() + 1 steps make a
 * period). Each value takes effect at the period after the DMA moved it (the
 * compare is preloaded), so the output starts with the duty it had (0 after
 * begin()) and the last value stays after the sequence, until play() or stop().
 * The array must stay unchanged and alive while busy(); with loop it plays on
 * and on until stop(). Needs the timer's update DMA request
 * (CH32RV_TIMn_DMA_UP in the variant) and its channel free: play() is false
 * otherwise. On CH32H41x every timer's request shares one DMAMUX channel, so
 * one sequence runs at a time. */
class CH32RVPwmSequence {
public:
    bool begin(int pin, uint32_t frequency);
    bool begin(const CH32RVTimerPad &pad, uint32_t frequency);
    void end();
    bool isAttached() const { return ch32rv_timer_lease_valid(&_lease); }
    uint32_t top() const { return _steps ? _steps - 1u : 0u; }
    uint32_t frequency() const { return _hz; }
    bool play(const uint16_t *duties, size_t count, bool loop = false);
    bool busy() const;
    /* The DMA stopped and the duty 0. */
    void stop();
    /* The duty now (between sequences), 0 .. top(). */
    bool write(uint32_t duty);

private:
    static void quiesce(void *self, uint8_t timer, uint8_t channel);
    const CH32RVTimerOwner _owner = {this, quiesce, this};   /* the manager keeps a pointer */
    CH32RVTimerLease _lease = {};
    uint32_t _base = 0, _steps = 0, _hz = 0;
    uint8_t _channel = 0, _pin = 0, _timer = 0;
    bool _wide = false, _dma = false;
    ch32rv_dma_line_t _line = {};
};
