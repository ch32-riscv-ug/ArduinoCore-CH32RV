/* Continuous ADC1 capture into two buffers by DMA, on the families whose ADC1
 * request is DMA1 channel 1 (ch32-device-data index/dma.csv): CH32V003 / V006,
 * V103, V20x, V205, V30x, L103 / M103, M030, X033 / X035 and the ADC1 pads of
 * X305 / X315; V407 / V467, where it is DMA1 channel 12; and H415 / H416 /
 * H417, where the DMAMUX routes it to DMA1 channel 1. The shape is CH32RVHsAdc's:
 *
 *     CH32RVAdcStream adc;
 *     adc.begin(A0, 100000);                // pin, conversions per second
 *     static uint16_t buf[2 * 512];
 *     adc.startContinuous(buf, 512);        // DMA fills the first half, then the second, ...
 *     if (const uint16_t *full = adc.readBuffer()) { ... }   // from loop()
 *
 * The rate: the ADC converts back to back (continuous mode, no timer used),
 * so the rate is the ADC clock over the sample time plus the conversion, in
 * eight steps. Which step gives what differs between families, and the tables
 * do not say, so begin() measures every step once and takes the fastest one at
 * or below the rate asked for; sampleRate() says what it got. It is what the
 * ADC does on this part, not a promise: a free-running ADC drifts with its
 * clock like everything else.
 *
 * Free-running also sets a floor: the slowest step is the ADC clock over its
 * longest sample time, and nothing slower is possible this way - about 47,600
 * conversions per second on a CH32V307 at 96 MHz, 250,000 on a CH32X035,
 * 99,000 on a CH32V407 at 200 MHz, 198,000 on a CH32H417 (measured
 * 2026-10-27). Ask for less and the slowest step is what you get.
 *
 * Triggered instead, a round waits for an event:
 *   - beginTimer(): a timer of the family's ADC trigger sources (TIM3's TRGO,
 *     TIM2's, a channel's compare - ch32rv_adc_trigger.h; taken through the
 *     timer manager, so false when none is free) ticks at the rate asked for,
 *     exact within its divisions of its clock and down to fractions of a
 *     hertz; the sample time is the longest that converts a round in 80 % of
 *     a tick.
 *   - beginExternal(): each edge (RISING, FALLING or CHANGE) of a pad that is
 *     the family's external trigger - any pad of bit 11 (bit 12 on X305 /
 *     X315) on most families, PD3 or PC2 on V00x, PA14 or PB6 on M030
 *     (isExternalTriggerPin()).
 * conversions() counts the rounds done since startContinuous(), so a sketch
 * can see each one land without waiting for a half to fill.
 *
 * Several pins (up to six) are converted in turn, one round per sample:
 * the buffer then holds pin 0, pin 1, ... pin 0, pin 1, ..., and
 * sampleRate() counts rounds.
 *
 * Values are the converter's own (0 .. 4095, or 0 .. 1023 on 10-bit parts),
 * without analogRead()'s scaling or calibration offset.
 *
 * Continuous capture: the two halves alternate. A half is the sketch's from
 * readBuffer() until the next readBuffer() call; when the DMA comes back to a
 * half the sketch has not taken, it is counted in overruns() and its data is
 * lost. While the capture runs, analogRead() must not be used; end() gives
 * the ADC back to it. */
#pragma once

#include <Arduino.h>
#include "internal/ch32rv_adc_trigger.h"

class CH32RVAdcStream {
public:
    static constexpr uint8_t MAX_PINS = 6;

    /* False when a pin is no ADC1 input. `rate`: conversions (rounds, with
     * several pins) per second wanted; the nearest step at or below is used.
     * Re-callable to change pins or rate. */
    bool begin(pin_size_t pin, uint32_t rate);
    bool begin(const pin_size_t *pins, uint8_t count, uint32_t rate);
    /* One round per tick of a timer at `rate` rounds per second. False when a
     * pin is no ADC1 input, no trigger timer is free, or none reaches the
     * rate (0, or faster than its clock). (CH32RVAdcStreamTrigger.cpp: linked
     * only by a sketch calling it or beginExternal().) */
    bool beginTimer(pin_size_t pin, uint32_t rate);
    bool beginTimer(const pin_size_t *pins, uint8_t count, uint32_t rate);
    /* One round per edge of `triggerPin` (`edge` RISING, FALLING or CHANGE).
     * `maxRate`: the most edges a second expected, for the sample time (the
     * longest that converts a round in 80 % of their period); 0 takes the
     * longest. False when a pin is no ADC1 input, `triggerPin` no trigger
     * pad, or `edge` one it cannot give (isExternalTriggerPin()). */
    bool beginExternal(pin_size_t pin, pin_size_t triggerPin, PinStatus edge, uint32_t maxRate = 0);
    bool beginExternal(const pin_size_t *pins, uint8_t count, pin_size_t triggerPin, PinStatus edge,
                       uint32_t maxRate = 0);
    /* Whether `pin` can be beginExternal()'s trigger on this part, for
     * `edge` (CHANGE is refused where the pad gives one edge only: V00x). */
    static bool isExternalTriggerPin(pin_size_t pin, PinStatus edge = RISING)
    {
        return ch32rv_adc_pin_trigger_valid(pin) && ch32rv_adc_pin_trigger_mode_ok((uint8_t)edge);
    }
    /* Stops a capture, frees the timer or the trigger pad, and gives the ADC
     * back to analogRead(). */
    void end();

    /* `buffer` holds 2 * `samples` values (with several pins, a multiple of
     * the pin count); the DMA fills one half, then the other. False if not
     * begun or the arguments are wrong. */
    bool startContinuous(uint16_t *buffer, uint16_t samples);
    /* CH32RVHsAdc's form: buf1 has to follow buf0 directly (the two halves of
     * one array) - this DMA has one circular buffer, not two. */
    bool startContinuous(uint16_t *buf0, uint16_t *buf1, uint16_t samples);
    void stop();
    /* The half filled since the last call, or nullptr when none is. */
    const uint16_t *readBuffer();
    /* Halves filled before the sketch took the one before them. */
    uint32_t overruns() const { return _overruns; }
    /* Halves filled since startContinuous(). */
    uint32_t buffersFilled() const { return _filled; }
    /* Rounds converted since startContinuous(): round n is at
     * buffer[(n * pins) % (2 * samples)]. */
    uint32_t conversions() const;

    /* Rounds per second: at the step begin() chose (measured), or the
     * timer's (its clock over its divisions, rounded); 0 before, and with an
     * external trigger. */
    uint32_t sampleRate() const { return _rate; }
    /* The sample-time step 0 (fastest) .. 7 chosen. */
    uint8_t sampleTimeStep() const { return _step; }
    /* The timer beginTimer() took (its number), 0 otherwise. */
    uint8_t triggerTimer() const;

    /* The ADC's analog watchdog over the stream: a converted value outside
     * low .. high (the converter's own units, as in the buffer) sets a flag
     * that watchdogTripped() returns and clears. `pinIndex` is one of the
     * begun pins (0 ..), or -1 for all of them. In hardware, at every
     * conversion: nothing to do per sample. Before or after
     * startContinuous(); false when not begun or out of range. */
    bool setWatchdog(uint16_t low, uint16_t high, int8_t pinIndex = -1);
    bool watchdogTripped();
    void clearWatchdog();

    /* For the interrupt handler. */
    void isr();

private:
    enum : uint8_t { TRIGGER_STOP, TRIGGER_START, TRIGGER_END };
    bool configure(uint8_t step);
    uint32_t measure(uint8_t step);
    uint8_t longestStep(uint32_t rate);
    /* The trigger's part of stop(), startContinuous() and end(): true when it
     * did it. A weak default returning false in CH32RVAdcStream.cpp; the real
     * one is CH32RVAdcStreamTrigger.cpp's, linked with beginTimer() /
     * beginExternal() - so a free-running stream carries none of it. */
    bool triggerControl(uint8_t op);
    /* The watchdog's settings back into CTLR1 after configure() rewrote it:
     * a weak no-op unless CH32RVAdcStreamWatchdog.cpp is linked (setWatchdog()). */
    void watchdogApply();
    int8_t _wdIndex = -2;               /* -2 off, -1 all pins, else a pin */
    uint16_t _wdLow = 0, _wdHigh = 0;

    uint8_t _channels[MAX_PINS] = {};
    uint8_t _count = 0, _step = 0;
    uint32_t _rate = 0;
    bool _begun = false, _running = false;
    uint8_t _trigger = 0;               /* 0 free-running, 1 a timer, 2 a pad */
    pin_size_t _triggerPin = 0;
    uint16_t *_buf = nullptr;
    uint16_t _samples = 0;
    volatile uint8_t _ready = 0;        /* bit n: half n full and not yet taken */
    volatile uint8_t _last = 0;         /* the half filled most recently */
    volatile uint32_t _overruns = 0, _filled = 0;
};
