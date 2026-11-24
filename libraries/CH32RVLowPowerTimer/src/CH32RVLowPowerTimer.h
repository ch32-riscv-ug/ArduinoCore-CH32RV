/* The low-power timer (LPTIM: CH32L103 / M103 one, CH32H41x two): a 16-bit
 * counter that keeps running in Stop - lightSleep() - on LSI or LSE, or on
 * the pulses of a pin with every clock off, and wakes the chip.
 *
 *     CH32RVLowPowerTimer lpt;               // LPTIM1 (CH32RVLowPowerTimer lpt2(2) on H41x)
 *     lpt.begin(CH32RVLowPowerTimerClock::Lsi, 32);   // LSI / 32 ticks
 *     lpt.setPeriod(lpt.clockHz() / 4);      // 250 ms
 *     lpt.onPeriod(tick);                    // in the timer's interrupt
 *     lpt.start();
 *     CH32RV.enableExtiLineWakeup(lpt.wakeLine());   // and lightSleep() ends at the period
 *
 *     lpt.beginCounter(PB10);                // count a pin's rising edges
 *     uint16_t n = lpt.count();
 *
 * The counter runs 0 .. period - 1 and starts over (onPeriod()), passing the
 * compare value on the way (onCompare()); output() puts the timer's output
 * on its pad: high from the compare value to the end of the period (low
 * with inverted), a PWM, or one pulse after startOnce().
 *
 * Clocks (CH32RVLowPowerTimerClock): Bus (PCLK1, the APB the timer is on;
 * stops in Stop), Hsi, Lse (only when it runs - the RTC Clock menu's LSE),
 * Lsi (started here; nominal, ±a few tens of %), each divided by 1, 2, 4 ..
 * 128.
 *
 * beginCounter() counts edges on the timer's CH1 pad: by default sampled by
 * an internal clock (no edge lost, at most that clock's rate); asynchronous,
 * the pin is the timer's clock and counts with every clock off (in Stop on
 * any clock), but up to the first five edges after start() are lost (RM
 * 17.3.9; 2-3 on H417).
 * beginEncoder() reads a quadrature encoder on CH1 / CH2 (every edge of
 * both: four counts a cycle) up and down between 0 and period - 1.
 *
 * The pads are the variant's CH32RV_LPTIMn_PADS; on L103 (AFIO) the pads
 * used together must be on one route (PB12-PB15 or PB5-PB7 / PB2). No
 * arduino-esp32 counterpart. */
#pragma once

#include <Arduino.h>

#if defined(CH32RV_CLKEN_LPTIM1_ADDR) || defined(CH32RV_CLKEN_LPTIM_ADDR)
#define CH32RV_HAS_LPTIM 1
#else
#define CH32RV_HAS_LPTIM 0
#endif

/* What the LPTIM counts (its CLKMX_SEL): Bus is PCLK1 (stops in Stop). */
enum class CH32RVLowPowerTimerClock : uint8_t { Bus = 0, Hsi = 1, Lse = 2, Lsi = 3 };

class CH32RVLowPowerTimer {
public:
    explicit CH32RVLowPowerTimer(uint8_t number = 1) : _number(number) {}
    ~CH32RVLowPowerTimer() { end(); }

    /* Ticks of `clock` / divider (1, 2, 4 .. 128). False when the timer
     * does not exist, is someone else's, or the clock does not run. */
    bool begin(CH32RVLowPowerTimerClock clock = CH32RVLowPowerTimerClock::Lsi, uint8_t divider = 1);
    /* Edges of `pin` (the timer's CH1 pad): RISING, FALLING or CHANGE (not
     * asynchronously). Sampled by `clock` unless asynchronous. */
    bool beginCounter(int pin, PinStatus edge = RISING, bool asynchronous = false,
                      CH32RVLowPowerTimerClock clock = CH32RVLowPowerTimerClock::Bus);
    /* A quadrature encoder on CH1 / CH2, sampled by `clock` (at least 4x
     * the edge rate). */
    bool beginEncoder(int pinA, int pinB, CH32RVLowPowerTimerClock clock = CH32RVLowPowerTimerClock::Bus);
    void end();

    /* The counter wraps every `ticks` (1 .. 65536; 65536 by default). */
    bool setPeriod(uint32_t ticks);
    /* onCompare() and the output's edge at this count (below the period;
     * until set, the period's last count, with the reload). */
    bool setCompare(uint32_t ticks);
    /* The output on `pin` (the timer's OUT pad). */
    bool output(int pin, bool inverted = false);

    bool start();       /* counting, over and over */
    bool startOnce();   /* one period, then stopped at the end */
    void stop();        /* stopped and back to 0 */
    uint16_t count();
    /* The encoder's direction now: 1 up, -1 down (0 when not an encoder). */
    int direction();

    /* In the timer's interrupt at the end of each period / at the compare
     * value; the interrupt (and the wake-up) is on only while one is set. */
    void onPeriod(void (*callback)());
    void onCompare(void (*callback)());

    /* Ticks a second (begin()'s clock / divider; 0 for an asynchronous counter). */
    uint32_t clockHz() const { return _hz; }
    /* CH32RV.enableExtiLineWakeup(wakeLine()): lightSleep() ends at the
     * timer's period end or compare match - the hardware's wake-up comes
     * from both whatever onPeriod() / onCompare() say; 0xFF when there is
     * none. */
    uint8_t wakeLine() const;

    void isr();

private:
    bool open(uint32_t cfgr);
    bool pad(int pin, uint8_t role);
    void write_arr(uint32_t v);
    void write_cmp(uint32_t v);

    uint8_t _number;
    int8_t _index = -1;
    bool _route_set = false;
    uint32_t _route_value = 0, _route_value2 = 0;
    uint32_t _hz = 0;
    uint32_t _period = 65536;
    uint32_t _compare = 65535;   /* the period's end until setCompare() */
    bool _compare_set = false;
    bool _encoder = false;
    void (*volatile _on_period)() = nullptr;
    void (*volatile _on_compare)() = nullptr;
};
