/* CH32RVLogicCapture on the CH32H41x UHSIF (CH32RVUhsif): its own file, so a
 * sketch that samples a GPIO port links none of it.
 *
 * Every clock the state machine latches the 16 data lines and pushes the
 * sample. Two threads take turns: thread 0 writes the first half of the
 * buffer, thread 1 the second, and the data counter switches them after
 * each half. A socket that has filled its buffer needs about 30 clocks (at
 * 200 MHz) to fetch its next descriptor and takes nothing meanwhile; with
 * one thread that gap opened at every buffer (measured: a 390.6 kHz PWM's
 * period came 32 samples short at each boundary at 200 MS/s). With two, the
 * other socket is writing during it, and the captured PWM keeps its period
 * across every boundary (CH32H417, 2026-11-11, 6.25 / 100 / 200 MS/s).
 *
 * States (inputs: a = DataCountHit):
 *   0 A0: left -> B0                (thread 0)
 *   1 B0: a ? -> A1 (reset, interrupt) : -> A0
 *   2 A1: left -> B1                (thread 1)
 *   3 B1: a ? -> A0 (reset, interrupt) : -> A1
 * The A states do not look at the counter: a reset is not seen by the
 * decision of the state it enters. Every transition latches and pushes, so
 * the machine alternates A and B and samples every clock. With the counter's
 * limit at half - 2 each thread pushes exactly `half` samples (measured:
 * limit 8189 and 8190 gave 8192, 8191 gave 8194). */
#include "CH32RVLogicCapture.h"

#if CH32RV_LOGIC_CAPTURE && CH32RV_LOGIC_DMAMUX

#include <CH32RVUhsif.h>

static CH32RVUhsif uhsif;
static uint8_t uhsifDivider;
static uint8_t nextHalf;

struct CH32RVLogicUhsif {
    static bool start(CH32RVLogicCapture &lc, uint16_t *buffer, uint32_t samples)
    {
        if (!buffer || ((uint32_t)(uintptr_t)buffer & 15u) || samples < 32u || samples > 65520u || (samples & 15u)) {
            return false;
        }
        const uint32_t half = samples / 2u;
        /* An interrupt per half: keep them at least 20 us apart (at 200 MS/s,
         * halves of 256 samples left the CPU nothing but the handler). */
        if ((uint64_t)half * 50000u < lc._rate) {
            return false;
        }
        constexpr CH32RVUhsifInput hit = CH32RVUhsifInput::DataCountHit, one = CH32RVUhsifInput::One;
        constexpr uint8_t S = CH32RVUhsif::SampleData;
        constexpr uint32_t P = CH32RVUhsif::Push, C = CH32RVUhsif::CountData,
                           R = CH32RVUhsif::ResetDataCount | CH32RVUhsif::Interrupt;
        const uint32_t t0 = CH32RVUhsif::thread(0), t1 = CH32RVUhsif::thread(1);
        const CH32RVUhsifState states[4] = {
            {{hit, one, one, one}, 0x0000, {1, S, P | C | t0}, {1, S, P | C | t0}},
            {{hit, one, one, one}, 0xAAAA, {0, S, P | C | t0}, {2, S, P | R | t1}},
            {{hit, one, one, one}, 0x0000, {3, S, P | C | t1}, {3, S, P | C | t1}},
            {{hit, one, one, one}, 0xAAAA, {2, S, P | C | t1}, {0, S, P | R | t0}},
        };
        const CH32RVUhsifTransition first = {0, S, P | CH32RVUhsif::ResetDataCount | t0};
        if (!uhsif.begin(uhsifDivider) || !uhsif.setStates(states, 4, first) || !uhsif.setDataCounter(half - 2u) ||
            !uhsif.setRing(0, buffer, (uint16_t)(half * 2u), 1, 0) ||
            !uhsif.setRing(1, buffer + half, (uint16_t)(half * 2u), 1, 0)) {
            return false;
        }
        lc._buf = buffer;
        lc._half = half;
        lc._ready = 0;
        lc._overruns = lc._filled = 0;
        nextHalf = 0;
        CH32RVLogicCapture *p = &lc;
        uhsif.onInterrupt([p] {
            p->halfFilled(nextHalf);
            nextHalf ^= 1u;
        });
        if (!uhsif.start()) {
            return false;
        }
        lc._running = true;
        return true;
    }

    static void stop(CH32RVLogicCapture &)
    {
        uhsif.stop();
    }

    static void end(CH32RVLogicCapture &)
    {
        uhsif.end();
    }

    static const CH32RVLogicCapture::Backend backend;
};

const CH32RVLogicCapture::Backend CH32RVLogicUhsif::backend = {CH32RVLogicUhsif::start, CH32RVLogicUhsif::stop,
                                                               CH32RVLogicUhsif::end};

bool CH32RVLogicCapture::begin(CH32RVLogicUhsifData, uint32_t sampleRate)
{
    if (sampleRate == 0) {
        return false;
    }
    end();
    /* SYSCLK / (divider + 1), divider 1..63. */
    uint32_t n = (CH32RV_CLOCK_SYSCLK_HZ + sampleRate / 2u) / sampleRate;
    if (n < 2u) {
        n = 2u;
    }
    if (n > 64u) {
        n = 64u;
    }
    uhsifDivider = (uint8_t)(n - 1u);
    _rate = CH32RV_CLOCK_SYSCLK_HZ / n;
    _backend = &CH32RVLogicUhsif::backend;
    _begun = true;
    return true;
}

#endif
