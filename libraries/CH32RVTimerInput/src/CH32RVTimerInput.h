/* Timer inputs: a signal's period and pulse width, quadrature encoders, and
 * each edge's time.
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
 *     CH32RVInputCapture edges;
 *     edges.begin(PB0, CHANGE);                // any channel of a timer
 *     CH32RVInputCapture::Edge e;
 *     while (edges.read(e)) { e.ticks; e.rising; }
 *
 * CH32RVPulseCapture and CH32RVEncoder: the timer does the work and keeps the
 * latest result in its registers; the sketch reads it when it wants to.
 * Nothing calls the sketch back, and reading does not move anything along: a
 * read() or getCount() that is never made loses nothing but the values in
 * between (docs/peripheral-a-design.ja.md 5.4, the time ladder in 3).
 * CH32RVInputCapture keeps every edge: its interrupt queues them, or hands
 * each to a callback.
 *
 * Pins: a channel of a timer on any route the chip offers (CH32RVTimer.h
 * ch32rv_timer_pad_find: the default pads, the ones a remap moves the timer
 * to, the per-pin alternate functions) - channel 1 or 2 for the pulse
 * capture and the encoder (the slave reset and the encoder mode are wired to
 * those two), any channel for CH32RVInputCapture. A pad on several timers
 * takes the first that is free. Each object holds the whole timer through
 * CH32RVTimer.h's lease, politely: when analogWrite(), tone() or Servo
 * already have it, begin() / attach*() return false. When one of them takes
 * the timer over later, the object stops (read() false, getCount() keeps the
 * last value, isAttached() false). A remap moves all of a timer's pads: the
 * timer's other pads on the old route stop being its.
 *
 * Timer ticks run at the timer's clock (ch32rv_timer_clock_hz), divided by
 * the prescaler each class picks. In stop and standby the timer stops with
 * the clock: counts are lost. */
#pragma once

#include <Arduino.h>
#include <CH32RVCallback.h>

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
    bool begin(const CH32RVTimerPad &pad, uint32_t minHz, uint8_t mode);
    void poll();

    const CH32RVTimerOwner _owner = {this, nullptr, nullptr};   /* the manager keeps a pointer */
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
    bool isAttached() const { return ch32rv_timer_lease_valid(&_lease); }

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
    bool attach(const CH32RVTimerPad &a, const CH32RVTimerPad &b, uint8_t edges);
    int64_t raw();
    static void onUpdate(void *self);

    const CH32RVTimerOwner _owner = {this, nullptr, nullptr};   /* the manager keeps a pointer */
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

/* Pulses counted by a timer: the edges of a signal on channel 1 or 2 clock
 * the counter itself (the timer's external clock mode 1), so no interrupt
 * runs per edge and pulses come as fast as the timer's clock can sample
 * them (about half of it). count() is 64 bits: the overflow is carried by the
 * update interrupt, as CH32RVEncoder's. Count over a known time for a
 * frequency. RISING or FALLING; CHANGE (both edges) on channel 1 only. */
class CH32RVPulseCounter {
public:
    bool begin(int pin, int edge = RISING, uint8_t mode = INPUT);
    void end();
    bool isAttached() const { return ch32rv_timer_lease_valid(&_lease); }

    uint64_t count();
    void clear();
    /* Ignore pulses shorter than about this many F_CPU cycles (0 .. 1024). */
    void setFilter(uint16_t cycles);

private:
    bool begin(const CH32RVTimerPad &pad, int edge, uint8_t mode);
    static void onUpdate(void *self);

    const CH32RVTimerOwner _owner = {this, nullptr, nullptr};
    CH32RVTimerLease _lease = {};
    uint32_t _base = 0;
    volatile uint32_t _wraps = 0;
    uint64_t _offset = 0;
    uint64_t _last = 0;
    bool _wide = false;
    uint8_t _filter = 0, _ckd = 0;
};

/* Each edge of a signal and its time, on any channel of a timer: the
 * capture latches the timer's count at the edge in hardware, and the
 * interrupt extends it to 64 bits and queues it (or hands it to a callback).
 * The time is right to the tick however late the interrupt runs; edges come
 * no closer than the interrupt can take them (a few microseconds), else the
 * newer of two is kept and lost() counts the other. CHANGE uses the
 * channel's pair as well (1-2, 3-4): one latches rising edges, the other
 * falling ones, so the two kinds never overwrite each other. */
class CH32RVInputCapture {
public:
    struct Edge {
        uint64_t ticks;     /* the timer's count at the edge */
        bool rising;
    };
    typedef CH32RVCallback<void(const Edge &)> Callback;

    /* edge: RISING, FALLING or CHANGE. tickHz: the count's rate, the timer
     * clock divided by a whole number (the nearest is taken; tickHz() says
     * which): 1 MHz by default, so ticks are microseconds. mode: INPUT,
     * INPUT_PULLUP or INPUT_PULLDOWN. False, and nothing held, when the pin
     * is no timer channel's pad, every timer it reaches is taken, or tickHz
     * is out of the timer's reach. */
    bool begin(int pin, int edge = CHANGE, uint32_t tickHz = 1000000, uint8_t mode = INPUT);
    void end();
    bool isAttached() const { return ch32rv_timer_lease_valid(&_lease); }

    /* Called from the timer's interrupt for each edge, in order - keep it
     * short. nullptr goes back to the queue. */
    void onEdge(Callback callback);
    /* Edges in the queue (up to 8), oldest first. */
    int available();
    bool read(Edge &out);
    /* Edges dropped: the queue was full, or two of a kind came closer than
     * the interrupt could take them. */
    uint32_t lost() const { return _lost; }

    uint32_t tickHz() const { return _tickHz; }
    /* The timer's count now, on the edges' scale. */
    uint64_t now();
    /* Ignore pulses shorter than about this many timer-clock cycles (0 ..
     * 1024), for a noisy line. Before begin(). */
    void setFilter(uint16_t cycles);

private:
    bool begin(const CH32RVTimerPad &pad, int edge, uint32_t tickHz, uint8_t mode);
    static void onInterrupt(void *self);
    static void onUpdate(void *self);
    void service(bool wrapped);
    void deliver(const Edge &e);

    static constexpr uint8_t QUEUE = 8;
    const CH32RVTimerOwner _owner = {this, nullptr, nullptr};   /* the manager keeps a pointer */
    CH32RVTimerLease _lease = {};
    uint32_t _base = 0;
    uint32_t _tickHz = 0;
    uint64_t _wraps = 0;
    Callback _callback;
    Edge _queue[QUEUE + 1];      /* a ring: one slot stays empty */
    volatile uint8_t _head = 0, _tail = 0;
    volatile uint32_t _lost = 0;
    uint8_t _own = 0, _pair = 0;  /* the channels latching the edges; _pair 0: one channel */
    bool _ownRising = true;
    bool _wide = false;
    uint8_t _filter = 0, _ckd = 0;
};

/* pulseIn() by a timer's input capture: the same arguments and result (the
 * pulse's width in microseconds, 0 on the timeout), with the edges latched in
 * hardware, so the width is exact to 1 us whatever interrupts run during the
 * pulse. The pin is a timer channel's pad on any route (any channel; its
 * pair, 1-2 or 3-4, latches the other edge), and the timer is borrowed whole
 * for the call, politely. Where the pin is no such pad, every timer it
 * reaches is taken, or the timer clock is no whole number of MHz, it is
 * pulseIn() itself (the software loop). Separate from pulseIn(), which stays
 * the software loop for every pin and links no timer code. */
unsigned long timerPulseIn(int pin, uint8_t state, unsigned long timeout = 1000000UL);
