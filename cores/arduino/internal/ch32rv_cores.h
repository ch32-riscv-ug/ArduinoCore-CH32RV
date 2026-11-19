/* CH32H41x's two cores, the parts the core itself needs: which core runs this, the hardware semaphore (HSEM,
 * RM 4.5) and the lock that keeps the registers both cores' drivers share whole.
 *
 * Core numbers are the RM's: core 0 is the V3F (it starts at reset), core 1 the V5F. PFIC_SCTLR bit 16 reads 1
 * on the V5F and 0 on the V3F (the EVT's NVIC_GetCurrentCoreID(); mhartid is not readable on the V3F).
 *
 * HSEM: 32 channels. A read of HSEM_RLRx locks a free channel for the core that read it (1-step lock, PID 0); a
 * write of this core's CID with LOCK = 0 to HSEM_Rx unlocks it, and only the core that locked it can. What the
 * read returns when it locks is told two ways: RM 4.5.1.1 says 0 (the channel was free), the EVT's
 * HSEM_FastTake() compares it with this core's LOCK | CID. Both mean taken here; the one left over - the read of a
 * channel this core holds already, which returns LOCK | this CID in both readings - is ruled out by the callers
 * (the core's lock never nests, ch32rv_hsem.c keeps the channels it holds). The bench measured the RM's (the
 * read 0, docs/h417-multicore.ja.md, 2026-10-22).
 *
 * The cores' lock: with the sketch on the V5F (CPU Core = V5F) the V3F may run the sketch's code as well
 * (setup1() / loop1(), CH32RV.startV3F()), and a few registers every driver writes by read-modify-write are
 * shared by the two: RCC's clock enables, a port's configuration (CFGLR / CFGHR), AFIO's pin functions and
 * EXTICR, EXTI's edge and enable bits. ch32rv_cores_lock() masks this core's interrupts and takes HSEM channel
 * CH32RV_HSEM_CORE_CHANNEL; ch32rv_cores_unlock() gives both back. Never nested. Elsewhere (one core runs the
 * sketch: every other family, and H41x with CPU Core = V3F, which leaves the V5F asleep) both are nothing. */
#pragma once

#include <stdint.h>

#include "internal/ch32rv_irq.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CH32RV_IRQN_HSEM)

#define CH32RV_HSEM_CHANNELS     32u
/* The ones the core takes: 31 for its own lock (below), 30 for the heap's (ch32rv_heap_lock.c). A sketch has the
 * others (0 to 29). */
#define CH32RV_HSEM_CORE_CHANNEL 31u
#define CH32RV_HSEM_HEAP_CHANNEL 30u
#define CH32RV_HSEM_RX(n)  (*(volatile uint32_t *)(0xE000C000u + 4u * (uint32_t)(n)))
#define CH32RV_HSEM_RLR(n) (*(volatile uint32_t *)(0xE000C100u + 4u * (uint32_t)(n)))
#define CH32RV_HSEM_LOCK   (1u << 31)

/* 1 on the V5F (core 1), 0 on the V3F (core 0). */
static inline uint32_t ch32rv_core_id(void)
{
    return (*(volatile uint32_t *)0xE000ED10u >> 16) & 1u;
}

/* The 1-step lock: one read, nonzero when this core has the channel (see above for the two readings). */
static inline int ch32rv_hsem_read_lock(uint32_t channel)
{
    const uint32_t got = CH32RV_HSEM_RLR(channel);
    return got == 0u || got == (CH32RV_HSEM_LOCK | (ch32rv_core_id() << 8));
}

/* Unlock a channel this core holds: its CID, PID 0, LOCK 0. Every write before it is done first (the V5F holds
 * writes back otherwise: the flash's lost page, TODO 2026-11-13). */
static inline void ch32rv_hsem_unlock(uint32_t channel)
{
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
    CH32RV_HSEM_RX(channel) = ch32rv_core_id() << 8;
}

#endif

#if defined(CH32RV_IRQN_HSEM) && defined(CH32RV_RUN_V5F)
/* Taken only once the V3F has been given a function: ch32rv_v3f_launch() sets ch32rv_cores_shared before the
 * V3F runs any (until then the V5F alone writes these registers). A sketch that never starts the V3F - no
 * startV3F(), no setup1() / loop1() - has no store to it at all, so with LTO it is the constant 0 and every lock
 * below folds away: such a sketch is the size it was without the lock. The lock itself is out of line
 * (ch32rv_cores.c). Not volatile on purpose (the folding needs a plain load); it changes once, in the V5F's own
 * thread, before the V3F starts. */
extern uint8_t ch32rv_cores_shared;
uint32_t ch32rv_cores_lock_taken(void);
void ch32rv_cores_unlock_taken(uint32_t irq);

/* Both test the flag, always inlined: at -Oz an outlined copy kept the test of the token alive. A pair never
 * spans the flag's change (it is set in ch32rv_v3f_launch(), called from the V5F's thread, never inside a pair). */
static inline __attribute__((always_inline)) uint32_t ch32rv_cores_lock(void)
{
    return ch32rv_cores_shared ? ch32rv_cores_lock_taken() : 0u;
}

static inline __attribute__((always_inline)) void ch32rv_cores_unlock(uint32_t irq)
{
    if (ch32rv_cores_shared) {
        ch32rv_cores_unlock_taken(irq);
    }
}
#else
static inline uint32_t ch32rv_cores_lock(void)
{
    return 0u;
}

static inline void ch32rv_cores_unlock(uint32_t irq)
{
    (void)irq;
}
#endif

#ifdef __cplusplus
}
#endif
