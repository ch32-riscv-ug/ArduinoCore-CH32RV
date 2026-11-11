/* CH32RVLogicCapture on the CH32H41x UHSIF (CH32RVUhsif): its own file, so a
 * sketch that samples a GPIO port links none of it.
 *
 * Every clock the state machine latches the 16 data lines and pushes the
 * sample. Two threads take turns: thread 0 writes the first half of the
 * buffer, thread 1 the second, and the data counter switches them after
 * each half. A socket stops after each buffer until CH32RVUhsif's interrupt
 * sends it on, and drops what comes meanwhile; with two threads the other
 * one is writing then. Checked on CH32H417 (2026-11-11) by stopping and
 * reading the whole buffer: a 390.625 kHz PWM kept its period (16 samples
 * at 6.25 MS/s, 512 at 200 MS/s) across every boundary and the wrap, and a
 * marker written before the start was gone everywhere.
 *
 * States (inputs: a = DataCountHit):
 *   0 A0: left -> B0                (thread 0)
 *   1 B0: a ? -> A1 (reset, interrupt) : -> A0
 *   2 A1: left -> B1                (thread 1)
 *   3 B1: a ? -> A0 (reset, interrupt) : -> A1
 * The A states do not look at the counter: a reset is not seen by the
 * decision of the state it enters. Every transition latches and pushes, so
 * the machine alternates A and B and samples every clock. A thread pushes
 * limit + 2 samples before it switches, so the limit is half - 2. */
#include "CH32RVLogicCapture.h"

#if CH32RV_LOGIC_CAPTURE && CH32RV_LOGIC_DMAMUX

#include <CH32RVUhsif.h>

static CH32RVUhsif uhsif;
static uint8_t uhsifDivider;
static CH32RVUhsifClock uhsifSource;
static uint8_t nextHalf;

struct CH32RVLogicUhsif {
    static bool start(CH32RVLogicCapture &lc, uint16_t *buffer, uint32_t samples)
    {
        if (!buffer || ((uint32_t)(uintptr_t)buffer & 31u) || samples < 64u || samples > 65472u || (samples & 63u)) {
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
        if (!uhsif.begin(uhsifDivider, uhsifSource) || !uhsif.setStates(states, 4, first) || !uhsif.setDataCounter(half - 2u) ||
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
    /* The nearest of SYSCLK / n and the 480 MHz USBHS PLL / n (n = 1..64),
     * up to maxUhsifRate(); SYSCLK when both are as near. */
    const CH32RVUhsifClock sources[2] = {CH32RVUhsifClock::Sysclk, CH32RVUhsifClock::UsbhsPll};
    uint32_t best = 0, bestErr = 0xFFFFFFFFu;
    for (const CH32RVUhsifClock src : sources) {
        const uint32_t hz = CH32RVUhsif::sourceHz(src);
        for (uint32_t n = 1; n <= 64; n++) {
            const uint32_t f = hz / n;
            if (f > maxUhsifRate()) {
                continue;
            }
            const uint32_t err = f > sampleRate ? f - sampleRate : sampleRate - f;
            if (err < bestErr) {
                bestErr = err;
                best = f;
                uhsifDivider = (uint8_t)(n - 1u);
                uhsifSource = src;
            }
        }
    }
    _rate = best;
    _backend = &CH32RVLogicUhsif::backend;
    _begun = true;
    return true;
}

#endif
