/* Interrupt priorities (PFIC IPRIOR), for sketches and libraries.
 *
 *     #include <ch32rv_priority.h>
 *     ch32rv_irq_set_priority(CH32RV_IRQN_TIM2, CH32RV_IRQ_PRIORITY(1));
 *     ch32rv_irq_set_priority(ch32rv_pin_irqn(PA1), CH32RV_IRQ_PRIORITY(0));
 *
 * A priority is the PFIC's byte, as WCH's NVIC_SetPriority() takes it:
 * smaller is more urgent. Its top CH32RV_IRQ_PREEMPT_BITS bits are the
 * preemption level - an interrupt of a more urgent level interrupts the
 * handler of a less urgent one (nesting) - and the rest order interrupts that
 * are pending together. CH32RV_IRQ_PRIORITY(level) is the byte of a level,
 * 0 (most urgent) to CH32RV_IRQ_PREEMPT_LEVELS - 1. The levels come from the
 * startup's INTSYSCR (and CSR 0xBC1) values and the EVTs' core_riscv.h:
 *
 *   2 levels (bit 7): V003, V00x, V103, V20x, V205, V4x7, X035, L103 / M103,
 *                     X305 / X315, H41x's V3F
 *   4 levels (bits 7-6): V30x (QingKe V4F), M030
 *   8 levels (bits 7-5): H41x's V5F (CPU Core = V5F)
 *
 * Nesting goes as deep as the hardware register stack (HPE): where the levels
 * are more than its depth (V30x: 4 levels, 3 deep) the interrupt that would
 * go one deeper waits for a handler to return (internal/ch32rv_interrupt.h).
 * CH32V103 has no HPE; its nesting is the PFIC's (enabled at reset, EVT).
 *
 * The contract with the core's own interrupts (SysTick for millis(), the
 * USARTs, the timers behind tone() / Servo / analogWrite()'s takeover, EXTI
 * for attachInterrupt(), USB, the RTC alarm, the WWDG's early warning):
 *  - The core never writes a priority. Every interrupt, the core's included,
 *    is at the reset value 0 (CH32RV_IRQ_PRIORITY_CORE: the most urgent
 *    level) until a sketch or library sets it here, so out of the box nothing
 *    nests.
 *  - A handler of yours set to a less urgent level is interrupted by the
 *    core's: millis() keeps counting and Serial keeps receiving through a
 *    long handler. This is the use the core is built for.
 *  - The core's handlers may be set to a less urgent level too (to let a
 *    handler of yours interrupt them). The core's thread-side code guards
 *    what it shares with its handlers by masking every interrupt
 *    (ch32rv_irq_save()), so that holds at any level; but a driver's
 *    functions are not re-entrant: a handler that interrupts the driver's own
 *    handler must not call that driver (Serial.write() from a handler that
 *    can interrupt the USART's, for one).
 *  - noInterrupts() / ch32rv_irq_save() mask every level.
 *  - On H41x the priorities are per core; the other core's are its own.
 *
 * The interrupt numbers are the generated CH32RV_IRQN_<name> (the EVTs'
 * names without _IRQn: CH32RV_IRQN_TIM2, CH32RV_IRQN_USART1, ...);
 * CH32RVTimerCapability's update_irq is a timer's, ch32rv_pin_irqn() a pin's
 * EXTI vector (shared with the other pins of its group: EXTI9_5 and the
 * like), CH32RV_IRQN_CORE_SYSTICK the SysTick that drives millis(). */
#pragma once

#include <stdint.h>

#include "Arduino.h"                  /* CH32RV_PIN_RESOLVE(), ch32rv_pad_is_valid() */
#include "internal/ch32rv_registers.h"

#if defined(CH32RV_RUN_V5F)
#define CH32RV_IRQ_PREEMPT_BITS 3
#elif defined(CH32RV_CORE_QINGKE_V4F) || defined(CH32RV_SERIES_CH32M030)
#define CH32RV_IRQ_PREEMPT_BITS 2
#else
#define CH32RV_IRQ_PREEMPT_BITS 1
#endif
#define CH32RV_IRQ_PREEMPT_LEVELS (1u << CH32RV_IRQ_PREEMPT_BITS)
/* The byte of preemption level `level` (0 = most urgent). */
#define CH32RV_IRQ_PRIORITY(level) ((uint8_t)((unsigned)(level) << (8 - CH32RV_IRQ_PREEMPT_BITS)))
#define CH32RV_IRQ_PRIORITY_CORE 0u
#define CH32RV_IRQ_PRIORITY_LEAST 0xFFu

/* The SysTick behind millis(): CH32H41x has one per core. */
#if defined(CH32RV_IRQN_SysTick1) && defined(CH32RV_RUN_V5F)
#define CH32RV_IRQN_CORE_SYSTICK CH32RV_IRQN_SysTick1
#elif defined(CH32RV_IRQN_SysTick0)
#define CH32RV_IRQN_CORE_SYSTICK CH32RV_IRQN_SysTick0
#else
#define CH32RV_IRQN_CORE_SYSTICK CH32RV_IRQN_SysTick
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Takes effect for the next time the interrupt is taken; a handler already
 * running keeps the level it entered with. */
static inline void ch32rv_irq_set_priority(uint32_t irqn, uint8_t priority)
{
    CH32RV_PFIC_IPRIOR(irqn) = priority;
}

/* What the PFIC holds (the bits it implements). */
static inline uint8_t ch32rv_irq_get_priority(uint32_t irqn)
{
    return CH32RV_PFIC_IPRIOR(irqn);
}

/* VTF (vector table free): up to CH32RV_IRQ_FAST_SLOTS interrupts the PFIC
 * enters at their handler's address directly, without fetching it from the
 * vector table first - a few cycles less latency for the ones that matter
 * (EVT INT/Interrupt_VTF). The handler stays the vector's own (the core's
 * dispatch, attachInterrupt()'s, a library's): only the way in changes.
 * true when `irqn` has a slot (it had one already, or a free one was taken);
 * false when all are in use or the series has none (V103's PFIC keeps them in
 * another form). ch32rv_irq_set_fast(irqn, false) gives the slot back. */
#if defined(CH32RV_SERIES_CH32V103)
#define CH32RV_IRQ_FAST_SLOTS 0
#elif defined(CH32RV_SERIES_CH32V002) || defined(CH32RV_SERIES_CH32V003) || defined(CH32RV_SERIES_CH32V004) || \
    defined(CH32RV_SERIES_CH32V005) || defined(CH32RV_SERIES_CH32V006) || defined(CH32RV_SERIES_CH32V007) || \
    defined(CH32RV_SERIES_CH32M007)
#define CH32RV_IRQ_FAST_SLOTS 2       /* QingKe V2 (the EVTs' SetVTFIRQ: num 0 .. 1) */
#else
#define CH32RV_IRQ_FAST_SLOTS 4
#endif
bool ch32rv_irq_set_fast(uint32_t irqn, bool on);
/* Whether `irqn` is entered by a VTF slot. */
bool ch32rv_irq_is_fast(uint32_t irqn);

/* A handler for an interrupt attached at run time, as attachInterrupt() does
 * for a pin: fn(arg) runs in the interrupt (machine mode, as any handler -
 * a plain function, no interrupt attribute). For an interrupt no handler is
 * linked for (the vector's Default_Handler): a library that wants a DMA
 * channel's or a timer's interrupt only when it gets that channel or timer
 * at run time. ch32rv_irq_attach() records fn and arg and enables the
 * interrupt; false when the vector has a linked handler of its own (a
 * library's DMA1_Channel1_IRQHandler, the core's USART1_IRQHandler: that one
 * stays), or all CH32RV_IRQ_ATTACH_SLOTS are in use. Attaching again
 * replaces fn and arg. ch32rv_irq_detach() disables it and gives the slot
 * back. Dispatched through Default_Handler by the interrupt's number
 * (mcause): a few cycles more than a linked handler, none of the vector
 * table moved to RAM (2026-11-20). On V103, whose table is jumps, a linked
 * handler is not detected: attach only what nothing links. */
#ifndef CH32RV_IRQ_ATTACH_SLOTS
#define CH32RV_IRQ_ATTACH_SLOTS 8
#endif
typedef void (*ch32rv_irq_handler_t)(void *arg);
bool ch32rv_irq_attach(uint32_t irqn, ch32rv_irq_handler_t fn, void *arg);
void ch32rv_irq_detach(uint32_t irqn);

/* The EXTI vector a pin's interrupt (attachInterrupt()) is taken on, or -1. */
static inline int ch32rv_pin_irqn(uint32_t pin)
{
    pin = CH32RV_PIN_RESOLVE(pin);
    if (!ch32rv_pad_is_valid(pin)) {
        return -1;
    }
    const uint32_t line = 1u << CH32RV_PIN_BIT(pin);
#define CH32RV_PIN_IRQN_GROUP(handler, mask, irqn) \
    if (line & (mask)) {                           \
        return (int)(irqn);                        \
    }
    CH32RV_EXTI_GROUPS(CH32RV_PIN_IRQN_GROUP)
#undef CH32RV_PIN_IRQN_GROUP
    return -1;
}

#ifdef __cplusplus
}
#endif
