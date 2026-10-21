/* The CH32H41x high-speed ADC (HSADC): 10 bits, up to 20 Msps, on HSADC_IN0-6
 * = PC0, PC1, PC2, PC3, PF8, PF9, PF10.
 *
 *     CH32RVHsAdc hsadc;
 *     hsadc.begin(PF8, 20000000);          // pin, samples per second
 *     uint16_t v = hsadc.read();           // one conversion, 0..1023
 *
 *     static uint16_t a[4096], b[4096];
 *     hsadc.startContinuous(a, b, 4096);   // DMA fills a, then b, then a ...
 *     if (const uint16_t *full = hsadc.readBuffer()) { ... }   // from loop()
 *
 * The rate: the HSADC clock is PLL_CLK (400 MHz) / (divider + 1), at most
 * 100 MHz, and a conversion takes 5 of its clocks - 20 Msps down to 1.25 Msps
 * in steps (begin() takes the nearest rate at or below the one asked for;
 * sampleRate() says which). Measured on CH32H417 (2026-10-21): 20.0 Msps
 * continuous, every buffer handed on, and full scale 1022-1023 with the pad
 * at 3.3 V.
 *
 * The input range is 0 .. the pin's own supply. PC0-PC3 are VIO18 pins: on
 * the evaluation board VIO18 starts at 1.2 V, so set it to 3.3 V (the VIO18
 * Supply menu or CH32RV.setVio18Millivolts(3300)) before putting 0-3.3 V on
 * them - their inputs are rated to VIO18 + 0.3 V. PF8-PF10 are VDDIO pins.
 *
 * Continuous capture: the two buffers alternate. A buffer is the sketch's
 * from readBuffer() until the next readBuffer() call; if the DMA comes back
 * to a buffer the sketch has not taken yet, it is counted in overruns() and
 * its data is lost. The buffers must be in RAM the DMA reaches (any global or
 * static array; not the V5F's TCMs). */
#pragma once

#include <Arduino.h>

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)

class CH32RVHsAdc {
public:
    /* False when `pin` is no HSADC input. Re-callable to change pin or rate. */
    bool begin(pin_size_t pin, uint32_t sampleRate = 20000000UL);
    void end();

    /* One conversion (0..1023). Not while continuous capture runs. */
    uint16_t read();

    /* Fill buf0, then buf1, then buf0 ... `samples` 16-bit values each
     * (3..65535). False if not begun or the arguments are wrong. */
    bool startContinuous(uint16_t *buf0, uint16_t *buf1, uint16_t samples);
    void stop();
    /* The buffer filled since the last call, or nullptr when none is. */
    const uint16_t *readBuffer();
    /* Buffers filled before the sketch took the one before them. */
    uint32_t overruns() const { return _overruns; }
    /* Buffers filled since startContinuous(). */
    uint32_t buffersFilled() const { return _filled; }

    uint32_t sampleRate() const { return _rate; }
    /* The input channel 0..6, or -1. */
    static int channelOf(pin_size_t pin);

    /* For the interrupt handler. */
    void isr();

private:
    uint32_t _rate = 0;
    uint8_t _div = 3, _channel = 0;
    bool _begun = false, _running = false;
    uint16_t *_buf[2] = {nullptr, nullptr};
    uint16_t _samples = 0;
    volatile uint8_t _ready = 0;        /* bit n: buffer n full and not yet taken */
    volatile uint8_t _last = 0;         /* the buffer filled most recently */
    volatile uint32_t _overruns = 0, _filled = 0;
};

#else
#error "CH32RVHsAdc is for CH32H415 / H416 / H417"
#endif
