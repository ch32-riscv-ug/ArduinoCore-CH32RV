/* Continuous ADC1 capture into two buffers by DMA, on the families whose ADC1
 * request is DMA1 channel 1 (ch32-device-data index/dma.csv): CH32V003 / V006,
 * V103, V20x, V205, V30x, L103 / M103, M030, X033 / X035 and the ADC1 pads of
 * X305 / X315. The shape is CH32RVHsAdc's:
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
 * conversions per second on a CH32V307 at 96 MHz, 250,000 on a CH32X035
 * (measured 2026-10-27). Ask for less and the slowest step is what you get;
 * for a slow steady rate, take every n-th value or use analogRead() from a
 * timer.
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

#if defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467) || defined(CH32RV_SERIES_CH32H415) || \
    defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
#error "CH32RVAdcStream: ADC1's DMA request is elsewhere on this series (V4x7: DMA2 channel 12, H41x: DMAMUX); not yet"
#endif

class CH32RVAdcStream {
public:
    static constexpr uint8_t MAX_PINS = 6;

    /* False when a pin is no ADC1 input. `rate`: conversions (rounds, with
     * several pins) per second wanted; the nearest step at or below is used.
     * Re-callable to change pins or rate. */
    bool begin(pin_size_t pin, uint32_t rate);
    bool begin(const pin_size_t *pins, uint8_t count, uint32_t rate);
    /* Stops a capture and gives the ADC back to analogRead(). */
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

    /* Rounds per second at the step begin() chose (measured), 0 before. */
    uint32_t sampleRate() const { return _rate; }
    /* The sample-time step 0 (fastest) .. 7 begin() chose. */
    uint8_t sampleTimeStep() const { return _step; }

    /* For the interrupt handler. */
    void isr();

private:
    bool configure(uint8_t step);
    uint32_t measure(uint8_t step);

    uint8_t _channels[MAX_PINS] = {};
    uint8_t _count = 0, _step = 0;
    uint32_t _rate = 0;
    bool _begun = false, _running = false;
    uint16_t *_buf = nullptr;
    uint16_t _samples = 0;
    volatile uint8_t _ready = 0;        /* bit n: half n full and not yet taken */
    volatile uint8_t _last = 0;         /* the half filled most recently */
    volatile uint32_t _overruns = 0, _filled = 0;
};
