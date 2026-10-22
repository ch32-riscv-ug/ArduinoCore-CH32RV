/* Masking interrupts from the code a sketch runs, without Arduino.h: the
 * save / restore pair the core, its libraries and CH32RVSyncKit build their
 * critical sections on. Arduino.h's interrupts() / noInterrupts() use the same
 * CSR choice.
 *
 * Where the global interrupt enable lives for the code a sketch runs:
 *
 * Sketches enter U mode (MPP = 0 in the board's CH32RV_MSTATUS_INIT) on every
 * QingKe V3B/V3F/V3V/V4 part, as WCH's own startups do, and the core enforces
 * it: mstatus is an illegal instruction there (mcause 2). Their CSR 0x800
 * (gintenr) holds the same enable bits (0x88), is user-accessible, and is what
 * WCH's __enable_irq/__disable_irq use on those cores. Measured on V4C - the
 * X035 on 2026-09-22, the L103 on 2026-09-23 - and on V4F, the V307, the same
 * day; V3B/V3F/V3V/V4A/V4B/V4J follow their EVT headers, not measured.
 *
 * The V2 parts and the V3A (CH32V103) run sketches in M mode and use mstatus.
 * The V3A has U mode but no gintenr (it reads 0 and ignores writes), so it is
 * put in M mode by its board (tools/generate/generate.py) rather than left
 * without a way to mask interrupts. The branch is on the core (CH32RV_CORE_*
 * from the generated variant), not on part names.
 *
 * None of this is atomic across cores: on CH32H41x the other core keeps
 * running. Masking is for sharing with this core's ISRs only. */
#pragma once

#include <stdint.h>

#include "ch32rv_pins.h"
#include "pins_arduino.h"

#if defined(CH32RV_CORE_QINGKE_V3B) || defined(CH32RV_CORE_QINGKE_V3F) || \
    defined(CH32RV_CORE_QINGKE_V3V) || \
    defined(CH32RV_CORE_QINGKE_V4A) || defined(CH32RV_CORE_QINGKE_V4B) || \
    defined(CH32RV_CORE_QINGKE_V4C) || defined(CH32RV_CORE_QINGKE_V4F) || \
    defined(CH32RV_CORE_QINGKE_V4J)
#define CH32RV_IRQ_IN_GINTENR 1
#elif defined(CH32RV_CORE_QINGKE_V2A) || defined(CH32RV_CORE_QINGKE_V2C) || \
      defined(CH32RV_CORE_QINGKE_V3A)
#define CH32RV_IRQ_IN_GINTENR 0
#else
#error "unknown QingKe core: say whether sketches run in U mode (gintenr) or M mode (mstatus)"
#endif

/* fence.i by its encoding: the boards' -march has no Zifencei, so the
 * assembler does not take the mnemonic. */
#define CH32RV_FENCE_I ".4byte 0x0000100f"

#ifdef __cplusplus
extern "C" {
#endif

static inline void ch32rv_irq_enable_all(void)
{
#if CH32RV_IRQ_IN_GINTENR || defined(CH32RV_CORE_QINGKE_V3A)
    const uint32_t mask = 0x88u;
#if CH32RV_IRQ_IN_GINTENR
    __asm__ volatile ("csrs 0x800, %0" :: "r"(mask) : "memory");
#else
    __asm__ volatile ("csrs mstatus, %0" :: "r"(mask) : "memory");
#endif
#else
    __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
#endif
}

static inline void ch32rv_irq_disable_all(void)
{
#if CH32RV_IRQ_IN_GINTENR
    const uint32_t mask = 0x88u;
    __asm__ volatile ("csrc 0x800, %0" :: "r"(mask) : "memory");
#elif defined(CH32RV_CORE_QINGKE_V3A)
    const uint32_t mask = 0x88u;   /* as in ch32rv_irq_save() below */
    __asm__ volatile ("csrc mstatus, %0\n\t" CH32RV_FENCE_I :: "r"(mask) : "memory");
#else
    __asm__ volatile ("csrci mstatus, 8" ::: "memory");
#endif
}

/* Save-and-disable / restore pair for short critical sections in the core and
 * its libraries. Same rule as above (mcause 2 in mstatus - seen when Wire's
 * requestFrom() masked interrupts around STOP on the X035, and when analogWrite()
 * took over a timer on the L103 and the V307; each sketch hung in the trap
 * handler). Nests: an inner pair restores what the outer one saw. */
static inline uint32_t ch32rv_irq_save(void)
{
    uint32_t old;
#if CH32RV_IRQ_IN_GINTENR
    const uint32_t mask = 0x88u;
    /* One csrrc, not csrr + csrc: as two statements GCC may give old and mask
     * the same register (old is not early-clobber), and csrc then clears every
     * set bit. On a V4F gintenr is a full view of mstatus, so that dropped FS
     * and the next ISR that saved an FP register trapped (mcause 2 on fsw). */
    __asm__ volatile ("csrrc %0, 0x800, %1" : "=r"(old) : "r"(mask) : "memory");
#elif defined(CH32RV_CORE_QINGKE_V3A)
    /* CH32V103: clearing MIE takes effect a few instructions late. A
     * critical section of two loads (a 64-bit copy) was still interrupted
     * between them - torn reads and lost Notify counts under a 10 kHz timer
     * interrupt (tests/instrumented/synckit, 2026-10-22). WCH's own
     * __disable_irq for this core clears MIE and MPIE and then runs fence.i;
     * so does this. */
    const uint32_t mask = 0x88u;
    __asm__ volatile ("csrrc %0, mstatus, %1\n\t" CH32RV_FENCE_I : "=r"(old) : "r"(mask) : "memory");
#else
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(old) :: "memory");
#endif
    return old;
}

static inline void ch32rv_irq_restore(uint32_t old)
{
#if CH32RV_IRQ_IN_GINTENR
    if (old & 0x88u) {
        const uint32_t mask = 0x88u;
        __asm__ volatile ("csrs 0x800, %0" :: "r"(mask) : "memory");
    }
#elif defined(CH32RV_CORE_QINGKE_V3A)
    if (old & 8u) {
        const uint32_t mask = 0x88u;
        __asm__ volatile ("csrs mstatus, %0" :: "r"(mask) : "memory");
    }
#else
    if (old & 8u) {
        __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
    }
#endif
}

#ifdef __cplusplus
}
#endif
