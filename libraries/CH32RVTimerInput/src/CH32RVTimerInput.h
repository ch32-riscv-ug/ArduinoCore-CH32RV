/* Timer inputs: a signal's period and pulse width, and quadrature encoders.
 *
 *     CH32RVPulseCapture pulse;
 *     pulse.begin(PA6);                         // a CH1 / CH2 pad of a timer
 *     CH32RVPulseCapture::Pulse p;
 *     if (pulse.read(p)) { p.periodMicros(); p.highMicros(); }
 *
 *     CH32RVEncoder knob;
 *     knob.attachFullQuad(PA6, PA7);           // CH1 and CH2 of one timer
 *     int64_t n = knob.getCount();
 *
 * The timer does the work and keeps the latest result in its registers; the
 * sketch reads it when it wants to. Nothing here calls the sketch back, and
 * reading does not move anything along: a read() or getCount() that is never
 * made loses nothing but the values in between (docs/peripheral-a-design.ja.md
 * 5.4, the time ladder in 3).
 *
 * Pins: channel 1 or 2 of a timer, on the board's default route - the pads
 * analogWrite() would drive from that timer (CH32RV_PWM_PIN_TO_TIMER). Each
 * object holds the whole timer through CH32RVTimer.h's lease, politely: when
 * analogWrite(), tone() or Servo already have it, begin() / attach*() return
 * false. When one of them takes the timer over later, the object stops
 * (read() false, getCount() keeps the last value, isAttached() false).
 *
 * Timer ticks run at F_CPU on every series (both APB prescalers stay at /1).
 * In stop and standby the timer stops with the clock: counts are lost. */
#pragma once

#include <Arduino.h>

#include "CH32RVTimer.h"

/* Period and high time of a periodic digital signal, in PWM-input mode: the
 * rising edge resets the counter and captures the period, the falling edge
 * captures the high time. No interrupt is used. */
class CH32RVPulseCapture {
public:
    struct Pulse {
        uint32_t period;    /* ticks from a rising edge to the next */
        uint32_t high;      /* ticks high in that period */
        uint32_t tickHz;    /* ticks per second */

        /* Rounded to the nearest microsecond. */
        uint32_t periodMicros() const { return (uint32_t)(((uint64_t)period * 1000000u + tickHz / 2u) / tickHz); }
        uint32_t highMicros() const { return (uint32_t)(((uint64_t)high * 1000000u + tickHz / 2u) / tickHz); }
        /* Rounded to the nearest hertz; 0 for none. */
        uint32_t frequencyHz() const { return period ? (tickHz + period / 2u) / period : 0u; }
        float frequency() const { return period ? (float)tickHz / (float)period : 0.0f; }
        /* High time over period, 0.0 .. 1.0. */
        float duty() const { return period ? (float)high / (float)period : 0.0f; }
    };

    /* pin: channel 1 or 2 of a timer. minHz: the slowest signal to measure;
     * it sets the tick (the resolution): F_CPU / minHz must fit the counter,
     * so a 16-bit timer at 96 MHz ticks every 1.5 us for 10 Hz and every
     * 10.4 ns for 1500 Hz. mode: INPUT, INPUT_PULLUP or INPUT_PULLDOWN.
     * False, and nothing held, when the pin is not such a pad or the timer
     * is taken. */
    bool begin(int pin, uint32_t minHz = 10, uint8_t mode = INPUT);
    void end();

    /* The newest whole period since the last read(), if there is one. The
     * first period after begin() and the first after a stall are dropped:
     * the counter did not start on their rising edge. */
    bool read(Pulse &out);
    /* The counter ran a whole 1 / minHz without a rising edge: the signal
     * stopped (held high or low - digitalRead() says which) or is slower
     * than minHz. It stays true until read() returns a period. */
    bool stalled();

    /* Ticks per second; 0 before begin(). */
    uint32_t tickHz() const { return _tickHz; }
    /* Ignore pulses shorter than about this many F_CPU cycles (0 .. 1024),
     * for a noisy line. Before or after begin(). */
    void setFilter(uint16_t cycles);

private:
    void poll();

    CH32RVTimerLease _lease = {};
    uint32_t _base = 0;
    uint32_t _tickHz = 0;
    uint8_t _periodCh = 0, _highCh = 0;
    bool _wide = false;          /* 32-bit counter */
    bool _dropNext = true;
    bool _overflowed = false;    /* since the last capture taken */
    bool _stalled = false;
    uint8_t _filter = 0, _ckd = 0;
};

/* A quadrature encoder on channels 1 and 2 of one timer, counted by the
 * timer's encoder mode. getCount() is 64 bits: the overflow of the 16-bit
 * counter is carried by the timer's update interrupt (inside the library;
 * no callback of the sketch's). A leading B counts up; swap the pins (or
 * negate) for the other way. Names as in ESP32Encoder. */
class CH32RVEncoder {
public:
    /* Every edge of A and B: 4 counts per cycle of a quadrature encoder. */
    bool attachFullQuad(int aPin, int bPin);
    /* Both edges of A: 2 counts per cycle. */
    bool attachHalfQuad(int aPin, int bPin);
    /* One count per cycle (the half-quad count halved). */
    bool attachSingleEdge(int aPin, int bPin);
    void detach();
    bool isAttached() const { return ch32TimerLeaseValid(&_lease); }

    int64_t getCount();
    void clearCount() { setCount(0); }
    void setCount(int64_t value);
    /* Stop and restart counting; edges in between are not counted. */
    bool pauseCount();
    bool resumeCount();

    /* INPUT, INPUT_PULLUP (the default: contacts to GND) or INPUT_PULLDOWN.
     * Before attach*(). */
    void setPull(uint8_t mode) { _pull = mode; }
    /* Ignore pulses shorter than about this many F_CPU cycles (0 .. 1024):
     * contact bounce. Before or after attach*(). */
    void setFilter(uint16_t cycles);

private:
    bool attach(int aPin, int bPin, uint8_t edges);
    int64_t raw();
    static void onUpdate(void *self);

    CH32RVTimerLease _lease = {};
    uint32_t _base = 0;
    volatile int32_t _wraps = 0;
    int64_t _offset = 0;
    int64_t _last = 0;
    bool _wide = false;
    bool _swapped = false;
    uint8_t _shift = 0;
    uint8_t _pull = INPUT_PULLUP;
    uint8_t _filter = 0, _ckd = 0;
};
