/* The hardware semaphore for sketches (ch32rv_multicore.h). A file of its own: linked only when called. */
#include "ch32rv_multicore.h"

#if defined(CH32RV_HAS_HSEM)

#include "Arduino.h"

/* The channels each core holds through these functions, by core number (internal/ch32rv_cores.h). Each word is
 * written by its own core only, with that core's interrupts masked. It tells a channel this core took from one
 * the other core holds - the read of a held channel returns LOCK | this CID, which one of the two readings of the
 * 1-step lock also returns when it locks. */
static volatile uint32_t held[2];

int ch32rv_hsem_try_take(uint32_t channel)
{
    if (channel >= CH32RV_HSEM_USER_CHANNELS) {
        return 0;
    }
    const uint32_t core = ch32rv_core_id();
    const uint32_t bit = 1u << channel;
    int ok = 0;
    const uint32_t irq = ch32rv_irq_save();
    if (!(held[core] & bit) && ch32rv_hsem_read_lock(channel)) {
        held[core] |= bit;
        ok = 1;
    }
    ch32rv_irq_restore(irq);
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
    return ok;
}

/* Between tries: the HSEM's register is not the shared SRAM, but the other core's code may be in the bank this
 * one's stack is in; a few nops cost nothing against a 70-280 ns lock (docs/h417-multicore.ja.md). */
static void pause(void)
{
    for (int i = 0; i < 16; i++) {
        __asm__ volatile ("nop");
    }
}

int ch32rv_hsem_take(uint32_t channel, uint32_t timeout_ms)
{
    if (channel >= CH32RV_HSEM_USER_CHANNELS || (held[ch32rv_core_id()] & (1u << channel))) {
        return 0;
    }
    const uint32_t start = millis();
    for (;;) {
        if (ch32rv_hsem_try_take(channel)) {
            return 1;
        }
        if (timeout_ms != CH32RV_WAIT_FOREVER && (uint32_t)(millis() - start) >= timeout_ms) {
            return 0;
        }
        pause();
    }
}

int ch32rv_hsem_release(uint32_t channel)
{
    if (channel >= CH32RV_HSEM_USER_CHANNELS) {
        return 0;
    }
    const uint32_t core = ch32rv_core_id();
    const uint32_t bit = 1u << channel;
    int ok = 0;
    const uint32_t irq = ch32rv_irq_save();
    if (held[core] & bit) {
        ch32rv_hsem_unlock(channel);    /* after every write made under it */
        held[core] &= ~bit;
        ok = 1;
    }
    ch32rv_irq_restore(irq);
    return ok;
}

#endif
