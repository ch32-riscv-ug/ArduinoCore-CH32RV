/* A whole GPIO port (16 inputs) sampled into RAM by DMA - a logic analyzer's
 * front end. CH32H415 / H416 / H417.
 *
 *     CH32RVLogicCapture logic;
 *     static uint16_t buf[32768];
 *     logic.begin(PORT_E, 10000000);          // port, samples per second
 *     logic.startContinuous(buf, 32768);      // halves of 16384, in turn
 *     if (const uint16_t *half = logic.readBuffer()) { ... }  // from loop()
 *
 *     uint32_t rate = logic.captureBurst(buf, 32768);   // one burst, fast
 *
 * Each sample is the port's input register (bit n = Pxn), whatever the pins'
 * modes; set them as inputs (or leave them in whatever use they have).
 *
 * Two ways, measured on CH32H417 at HCLK 100 MHz (2026-10-21):
 *  - Continuous: TIM6's update requests DMA2 channel 8 (DMAMUX request 122),
 *    which reads the port into a circular buffer. A DMA transfer from a port
 *    takes 8 HCLK clocks and a request that comes while one is in progress is
 *    dropped, so one channel stops at F_CPU / 8 = 12.5 MS/s. With the USB
 *    streaming the buffer at the same time it loses samples below that too:
 *    the USB and this DMA contend whenever they are in the same 128 KB SRAM
 *    bank (a ring in one bank: 151 ppm of a 100 kHz PWM's periods at 9.09
 *    MS/s; the ring's halves on either side of a bank boundary: none at up to
 *    11.1 MS/s). begin() clamps to F_CPU / 11 for now; where the buffer goes
 *    and a two-channel 20 MS/s mode are being worked out - measurements in
 *    docs/h417-capture.ja.md (2026-10-21).
 *  - Burst (captureBurst()): the DMA copies the port back to back with no
 *    timer, 5 HCLK clocks a sample = 20 MS/s, for up to 65535 samples. The
 *    interval is the DMA's, not a timer's: when the CPU or another DMA uses
 *    the bus at the same time a sample comes a clock late (16-bit: 6 of ~320
 *    PWM periods were off by a few samples). Interrupts are off while it
 *    runs.
 *
 * TIM6 is taken through the core's timer resource manager (CH32RVTimer.h),
 * so analogWrite() / tone() / Servo leave it alone and the other way round.
 * The buffer must be in RAM the DMA reaches (globals and statics). */
#pragma once

#include <Arduino.h>

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)

#include "CH32RVTimer.h"

enum CH32RVLogicPort : uint8_t { PORT_A = 0, PORT_B, PORT_C, PORT_D, PORT_E, PORT_F };

class CH32RVLogicCapture {
public:
    /* The fastest continuous rate that kept every sample: F_CPU / 11. */
    static constexpr uint32_t maxContinuousRate() { return F_CPU / 11u; }

    /* `sampleRate` is clamped to maxContinuousRate() and rounded to what the
     * timer reaches (F_CPU / an integer); sampleRate() says what it got. False
     * when the port does not exist or TIM6 is held by someone else. */
    bool begin(CH32RVLogicPort port, uint32_t sampleRate);
    void end();
    uint32_t sampleRate() const { return _rate; }

    /* Fill `buffer` round and round, `samples` 16-bit values (even, up to
     * 65534); each half is handed over when full. */
    bool startContinuous(uint16_t *buffer, uint32_t samples);
    void stop();
    /* The half filled since the last call (samples / 2 values), or nullptr. */
    const uint16_t *readBuffer();
    /* Halves filled again before the sketch took them (their data is lost). */
    uint32_t overruns() const { return _overruns; }
    uint32_t halvesFilled() const { return _filled; }

    /* One burst of `samples` (1..65535) at the DMA's own speed, no timer;
     * returns the rate it ran at (samples per second, from the time taken),
     * 0 on bad arguments or when the DMA did not finish. Stops a continuous
     * capture first. */
    uint32_t captureBurst(uint16_t *buffer, uint16_t samples);

    /* For the interrupt handler. */
    void isr();

private:
    uint32_t _port = 0;           /* the INDR address */
    uint32_t _rate = 0;
    CH32RVTimerLease _lease = {};
    bool _begun = false, _running = false;
    uint16_t *_buf = nullptr;
    uint32_t _half = 0;
    volatile uint8_t _ready = 0, _last = 0;
    volatile uint32_t _overruns = 0, _filled = 0;
};

#else
#error "CH32RVLogicCapture is for CH32H415 / H416 / H417"
#endif
