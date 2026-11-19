/* The DAC beyond analogWrite(): waves the converter makes itself, and
 * samples a DMA channel feeds it, each step on a timer's trigger (TRGO), so
 * the CPU does nothing while they run.
 *
 *   CH32RVDac dac(1);                  // DAC1 on its pad (CH32RV_DAC1_PIN: PA4)
 *   dac.begin();
 *   dac.triangle(12, 100000);          // 0 .. 4095 and back, a step every 10 us
 *   dac.noise(8, 50000);               // 8 bits of the converter's LFSR, 50 kHz
 *   dac.play(sine, 64, 64000);         // 64 samples, 1 kHz, round and round
 *   dac.write(2048);                   // a plain value again (the wave stops)
 *
 * The triangle steps by one LSB per trigger between base and base + 2^bits -
 * 1 (the converter's own counter); a full 12-bit triangle at stepHz is
 * stepHz / 8190 Hz. The noise is the converter's LFSR, its low `bits` bits
 * added to base. Samples are 12-bit, right-aligned (0 .. 4095).
 *
 * The step's timer is one of the timers the DAC can trigger on (the
 * variant's CH32RV_DAC_TRIGGERS: TIM6, TIM7 first, then TIM8, TIM5, TIM2,
 * TIM4), held whole while a wave runs; the DMA channel is the variant's
 * (CH32RV_DACn_DMA), claimed while samples play. Where the series has no DAC
 * (CH32RV_DACn_PIN) everything returns false. */
#pragma once

#include <Arduino.h>
#include <stdint.h>

class CH32RVDac {
public:
    explicit CH32RVDac(uint8_t channel = 1) : _channel(channel) {}

    /* The channel on its pad, its output buffer on, at 0. */
    bool begin();
    void end();

    /* A value now (0 .. 4095); a wave that ran stops first. */
    bool write(uint16_t value);
    /* The converter's triangle: base .. base + 2^bits - 1 and back, one LSB a
     * step, stepHz steps a second (bits 1 .. 12). */
    bool triangle(uint8_t bits, uint32_t stepHz, uint16_t base = 0);
    /* The converter's noise: its LFSR's low `bits` bits (1 .. 12) added to
     * base, a new value stepHz times a second. */
    bool noise(uint8_t bits, uint32_t stepHz, uint16_t base = 0);
    /* samples[0 .. count - 1] (12-bit), one every 1 / sampleHz, over and
     * over until stop() or write(). The buffer must stay valid while it plays;
     * changing it plays the change. */
    bool play(const uint16_t *samples, uint16_t count, uint32_t sampleHz);
    /* Both channels on one trigger (the converter's dual holding register,
     * RD12BDHR): pairs[0] / pairs[1] are DAC1's / DAC2's first values,
     * pairs[2] / pairs[3] the next, ... `count` pairs, sampleHz of them a
     * second, over and over; the two outputs change at the same moment. The
     * array 4-byte aligned (a word a pair; alignas(4)). On a DAC1 object
     * (false on DAC2's); it sets DAC2 up itself, and a DAC2 object must not be
     * playing meanwhile. stop() / write() end both. */
    bool playBoth(const uint16_t *pairs, uint16_t count, uint32_t sampleHz);
    /* The wave or samples stop; the output keeps its last value. */
    void stop();

    /* The steps a second the timer makes (the nearest it can), 0 when no
     * wave runs. */
    uint32_t stepRate() const { return _rate; }
    bool running() const { return _timer != 0; }

private:
    bool startTimer(uint32_t hz, uint8_t &tsel);
    void stopTimer();

    uint8_t _channel;
    bool _begun = false;
    bool _dma = false;
    bool _both = false;
    uint8_t _timer = 0;
    uint32_t _rate = 0;
};
