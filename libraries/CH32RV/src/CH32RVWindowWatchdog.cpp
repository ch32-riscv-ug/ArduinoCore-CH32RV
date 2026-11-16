/* CH32RV.wwdgEnable() / wwdgFeed() (CH32RVSystem.h): the window watchdog. A
 * file of its own, so that a sketch that never uses it links none of it, its
 * interrupt handler included (this library is an archive).
 *
 * The block is the same on every family (the reference manuals' WWDG chapter,
 * the EVT's WWDG/WWDG): at 0x40002C00, a 7-bit down-counter clocked by
 * PCLK1 / 4096 / 2^WDGTB (WDGTB 0-3). The chip resets when the counter passes
 * 0x40 to 0x3F, or when it is written (fed) while still above the window
 * value W - a feed is allowed only while 0x3F < counter < W. The early-wakeup
 * interrupt (EWI) comes at 0x40, one count before the reset. Once WDGA is set,
 * only a reset clears it; EWI likewise. */
#include "CH32RVSystem.h"

#include "Arduino.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_interrupt.h"

#define WWDG_CTLR   CH32RV_REG32(0x40002C00u)
#define WWDG_CFGR   CH32RV_REG32(0x40002C04u)
#define WWDG_STATR  CH32RV_REG32(0x40002C08u)
#define WWDG_WDGA   (1u << 7)
#define WWDG_EWI    (1u << 9)

namespace {

/* One count at each WDGTB, in nanoseconds - constants, so V003 divides none of
 * this at run time in 64 bits. At most 4096 x 8 / 8 MHz = 4.1 ms. */
#define WWDG_TICK_NS(b) ((uint32_t)((4096ull << (b)) * 1000000000ull / (uint64_t)CH32RV_PCLK1_HZ))
const uint32_t tick_ns[4] = {WWDG_TICK_NS(0), WWDG_TICK_NS(1), WWDG_TICK_NS(2), WWDG_TICK_NS(3)};

/* 63 counts at most: with no window the window value is 0x7F, and the counter
 * must stay below it to be fed at once (a feed at 0x7F itself resets). */
constexpr uint32_t max_ticks = 63u;

uint8_t counter;                 /* T: what a feed writes */
void (*volatile early)();

}  // namespace

extern "C" CH32RV_INTERRUPT void WWDG_IRQHandler(void)
{
    WWDG_STATR = 0;              /* EWIF */
    void (*const fn)() = early;
    if (fn) {
        fn();
    }
}

namespace arduino {

uint32_t CH32RVSystem::wwdgMaxTimeoutUs()
{
    return max_ticks * (tick_ns[3] / 1000u);
}

bool CH32RVSystem::wwdgEnable(uint32_t timeoutUs, uint32_t windowUs, void (*earlyWarning)())
{
    if (timeoutUs > 4000000u || windowUs >= timeoutUs) {
        return false;            /* far beyond any WDGTB at any clock this core runs */
    }
    /* The smallest prescaler whose 63 counts hold the timeout: the finest
     * resolution. Counts are rounded down - the reset comes no later than
     * asked, up to one count earlier (the prescaler runs freely, so the first
     * count after a feed is a partial one). */
    uint8_t b = 0;
    uint32_t ticks = 0;
    for (; b < 4u; b++) {
        ticks = timeoutUs * 1000u / tick_ns[b];
        if (ticks <= max_ticks) {
            break;
        }
    }
    if (b == 4u || ticks == 0u) {
        return false;            /* longer than 63 counts at /8, or shorter than one at /1 */
    }
    /* The window: a feed before windowUs resets the chip. k counts must have
     * passed (rounded up), so a feed from windowUs on is always allowed and
     * one more than a count before it never is. */
    uint32_t window = 0x7Fu;
    if (windowUs) {
        const uint32_t k = (windowUs * 1000u + tick_ns[b] - 1u) / tick_ns[b];
        if (k + 1u >= ticks) {
            return false;        /* nothing left between the window and the timeout */
        }
        window = 0x3Fu + ticks - k + 1u;
    }
    counter = (uint8_t)(0x3Fu + ticks);
    early = earlyWarning;
    ch32rv_clock_enable(WWDG);
    WWDG_CFGR = ((uint32_t)b << 7) | window | (earlyWarning ? WWDG_EWI : 0u);
    WWDG_STATR = 0;
    if (earlyWarning) {
        ch32rv_irq_enable(CH32RV_IRQN_WWDG);
    }
    WWDG_CTLR = WWDG_WDGA | counter;
    return true;
}

void CH32RVSystem::wwdgFeed()
{
    if (counter) {
        WWDG_CTLR = counter;
    }
}

}  // namespace arduino
