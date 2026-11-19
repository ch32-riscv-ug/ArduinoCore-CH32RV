/* ADC1 runs moved by DMA: the parts CH32RVAdcStream and analogContinuous()
 * share. One run at a time - there is one ADC1 and one DMA channel its
 * requests reach - and analogRead() must wait until it ends.
 *
 * The DMA channel differs between families and is this file's to know, not
 * its users': DMA1 channel 1 everywhere (ch32-device-data dma.csv), channel
 * 12 on V407 / V467 (EVT ADC_DMA; its flags are in DMA1_EXTEN at 0xF0, four
 * bits per channel from channel 8 - ch32v4x7_dma.h DMA1_FLAG_GL8 0x40000001,
 * so channel 12's from bit 16), and on H41x DMAMUX
 * channel 1, which feeds DMA1 channel 1, set to ADC1's request 120 (EVT
 * ADC_DMA writes 0x78; the register holds the number minus one).
 *
 * The helpers are static, compiled into each user: a sketch with
 * CH32RVAdcStream alone gets the same code it had when they were the
 * library's own. The DMA's interrupt is ch32rv_adc_run.c's, linked by
 * ch32rv_adc_run_link(); it calls each user's hook, a weak empty default
 * where that user is not linked. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "internal/ch32rv_adc_trigger.h"
#include "internal/ch32rv_dma.h"
#include "internal/ch32rv_registers.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CH32RV_ADC_DMA1_BASE   0x40020000u
#if defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
#define CH32RV_ADC_DMA_INTFR   CH32RV_REG32(CH32RV_ADC_DMA1_BASE + 0xF0u)
#define CH32RV_ADC_DMA_INTFCR  CH32RV_REG32(CH32RV_ADC_DMA1_BASE + 0xF4u)
#define CH32RV_ADC_DMA_CH_BASE (CH32RV_ADC_DMA1_BASE + 0xD0u)
#define CH32RV_ADC_DMA_SHIFT   16u
#define CH32RV_ADC_DMA_CHANNEL 12u
#define CH32RV_ADC_DMA_IRQN    CH32RV_IRQN_DMA1_Channel12
#define CH32RV_ADC_DMA_HANDLER DMA1_Channel12_IRQHandler
#else   /* every other family, and H41x through the DMAMUX */
#define CH32RV_ADC_DMA_INTFR   CH32RV_REG32(CH32RV_ADC_DMA1_BASE + 0x00u)
#define CH32RV_ADC_DMA_INTFCR  CH32RV_REG32(CH32RV_ADC_DMA1_BASE + 0x04u)
#define CH32RV_ADC_DMA_CH_BASE (CH32RV_ADC_DMA1_BASE + 0x08u)
#define CH32RV_ADC_DMA_SHIFT   0u
#define CH32RV_ADC_DMA_CHANNEL 1u
#define CH32RV_ADC_DMA_IRQN    CH32RV_IRQN_DMA1_Channel1
#define CH32RV_ADC_DMA_HANDLER DMA1_Channel1_IRQHandler
#endif
#if defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
#define CH32RV_ADC_DMAMUX_C0_3 CH32RV_REG32(0x40020800u)   /* mux channels 1..4, a byte each */
#define CH32RV_ADC_DMA_MUX()   (CH32RV_ADC_DMAMUX_C0_3 = (CH32RV_ADC_DMAMUX_C0_3 & ~0x7Fu) | (120u - 1u))
#else
#define CH32RV_ADC_DMA_MUX()   ((void)0)
#endif
/* ADC1's own interrupt (end of an injected round, ...): its vector's name differs between families. The
 * handler is ch32rv_adc_irq.c's, linked by ch32rv_adc_irq_link(); it calls ch32rv_adc_injected_irq(), a weak
 * empty default where CH32RVAdcInjected is not linked. */
#if defined(CH32RV_IRQN_ADC1_2)
#define CH32RV_ADC_IRQN        CH32RV_IRQN_ADC1_2
#define CH32RV_ADC_IRQ_HANDLER ADC1_2_IRQHandler
#elif defined(CH32RV_IRQN_ADC1)
#define CH32RV_ADC_IRQN        CH32RV_IRQN_ADC1
#define CH32RV_ADC_IRQ_HANDLER ADC1_IRQHandler
#elif defined(CH32RV_IRQN_ADC)
#define CH32RV_ADC_IRQN        CH32RV_IRQN_ADC
#define CH32RV_ADC_IRQ_HANDLER ADC_IRQHandler
#endif
void ch32rv_adc_irq_link(void);
void ch32rv_adc_injected_irq(void);

#define CH32RV_ADC_DMA_CFGR    CH32RV_REG32(CH32RV_ADC_DMA_CH_BASE + 0x00u)
#define CH32RV_ADC_DMA_CNTR    CH32RV_REG32(CH32RV_ADC_DMA_CH_BASE + 0x04u)
#define CH32RV_ADC_DMA_PADDR   CH32RV_REG32(CH32RV_ADC_DMA_CH_BASE + 0x08u)
#define CH32RV_ADC_DMA_MADDR   CH32RV_REG32(CH32RV_ADC_DMA_CH_BASE + 0x0Cu)

/* INTFR / INTFCR, the channel's four bits. */
#define CH32RV_ADC_DMA_GIF   (1u << (CH32RV_ADC_DMA_SHIFT + 0u))
#define CH32RV_ADC_DMA_TCIF  (1u << (CH32RV_ADC_DMA_SHIFT + 1u))
#define CH32RV_ADC_DMA_HTIF  (1u << (CH32RV_ADC_DMA_SHIFT + 2u))
#define CH32RV_ADC_DMA_TEIF  (1u << (CH32RV_ADC_DMA_SHIFT + 3u))
#define CH32RV_ADC_DMA_ALL   (CH32RV_ADC_DMA_GIF | CH32RV_ADC_DMA_TCIF | CH32RV_ADC_DMA_HTIF | CH32RV_ADC_DMA_TEIF)

/* CFGR */
#define CH32RV_ADC_DMA_EN     (1u << 0)
#define CH32RV_ADC_DMA_TCIE   (1u << 1)
#define CH32RV_ADC_DMA_HTIE   (1u << 2)
#define CH32RV_ADC_DMA_CIRC   (1u << 5)
#define CH32RV_ADC_DMA_MINC   (1u << 7)
#define CH32RV_ADC_DMA_16BIT  ((1u << 8) | (1u << 10))   /* PSIZE and MSIZE 16 bits */
#define CH32RV_ADC_DMA_PL_HI  (2u << 12)

#define CH32RV_ADC_CTLR1_SCAN  (1u << 8)
#define CH32RV_ADC_CTLR2_CONT  (1u << 1)
#define CH32RV_ADC_CTLR2_DMA   (1u << 8)
#define CH32RV_ADC_RSQR2       CH32RV_REG32(CH32RV_ADC1_BASE + 0x30u)

#define CH32RV_ADC_DMA_DONE() __asm__ volatile("fence rw, rw" ::: "memory")

#define CH32RV_ADC_RUN_STATIC static __attribute__((unused))

/* The DMA channel for this run's user alone, clocked and, on H41x, given
 * ADC1's request. False, nothing touched, while another holds it. The claim
 * goes through ch32rv_dma_claims (internal/ch32rv_dma.h): without another
 * user of DMA channels in the sketch nothing can hold this one, and the ADC's
 * users pay nothing for it. */
CH32RV_ADC_RUN_STATIC bool ch32rv_adc_dma_take(void)
{
    if (ch32rv_dma_claims && !ch32rv_dma_claims->claim(1, CH32RV_ADC_DMA_CHANNEL)) {
        return false;
    }
    ch32rv_clock_enable(DMA1);
    CH32RV_ADC_DMA_MUX();
    return true;
}

/* The channel let go (the run stopped already). */
CH32RV_ADC_RUN_STATIC void ch32rv_adc_dma_give(void)
{
    if (ch32rv_dma_claims) {
        ch32rv_dma_claims->release(1, CH32RV_ADC_DMA_CHANNEL);
    }
}

/* Stopping by clearing CONT lets the conversion in flight, and with several
 * pins the rest of the sequence, finish after the stop: a result left behind,
 * its EOC set, which analogRead() after end() returned at once instead of its
 * own pin (the stream's last pin read; CH32X035, 2026-11-05). So a stop powers
 * the ADC down (ADON 0 drops whatever is converting) and up again (ADON 0 to 1
 * starts nothing: measured, no conversion follows without SWSTART), the mode
 * is set while it waits, and a start is the one write that sets SWSTART. */
CH32RV_ADC_RUN_STATIC void ch32rv_adc_restart(uint32_t mode)
{
    CH32RV_ADC_CTLR2 = mode;
    CH32RV_ADC_CTLR2 = mode | CH32RV_ADC_CTLR2_ADON;
    for (volatile int settle = 0; settle < 1000; settle++) {   /* as analogRead()'s power-up */
    }
    (void)CH32RV_ADC_RDATAR;
}

/* What CTLR2 holds besides the bits a run sets (analogRead()'s trigger
 * selection, a temperature sensor enable). */
CH32RV_ADC_RUN_STATIC uint32_t ch32rv_adc_mode(void)
{
    return CH32RV_ADC_CTLR2 & ~(CH32RV_ADC_CTLR2_ADON | CH32RV_ADC_CTLR2_CONT | CH32RV_ADC_CTLR2_DMA |
                                CH32RV_ADC_CTLR2_SWSTART);
}

/* Stopped, powered down and up (ch32rv_adc_restart), with no DMA request
 * left over. A conversion the DMA never took - the ADC still converting when
 * a counted transfer (a measurement) ran out - leaves a request that
 * outlives the power-down and a read of RDATAR: the next channel enable moved
 * one stale value at once, before the ADC started, and every pin after it sat
 * one place late (pin 0's place holding the last pin's - X A B C A B C,
 * CH32X035 / V003 with two or three pins, 2026-11-07). It is taken here into
 * a dummy, with the ADC's DMA bit set so the ADC sees it answered; with none
 * pending nothing moves. */
CH32RV_ADC_RUN_STATIC void ch32rv_adc_halt(void)
{
    const uint32_t mode = ch32rv_adc_mode();
    ch32rv_adc_restart(mode);
    static volatile uint16_t dummy;
    CH32RV_ADC_DMA_CFGR = 0;
    CH32RV_ADC_CTLR2 = mode | CH32RV_ADC_CTLR2_ADON | CH32RV_ADC_CTLR2_DMA;
    CH32RV_ADC_DMA_PADDR = (uint32_t)(uintptr_t)&CH32RV_ADC_RDATAR;
    CH32RV_ADC_DMA_MADDR = (uint32_t)(uintptr_t)&dummy;
    CH32RV_ADC_DMA_CNTR = 1;
    CH32RV_ADC_DMA_CFGR = CH32RV_ADC_DMA_16BIT | CH32RV_ADC_DMA_PL_HI | CH32RV_ADC_DMA_EN;
    for (volatile int settle = 0; settle < 100 && CH32RV_ADC_DMA_CNTR; settle++) {
    }
    CH32RV_ADC_DMA_CFGR = 0;
    CH32RV_ADC_CTLR2 = mode | CH32RV_ADC_CTLR2_ADON;
    CH32RV_ADC_DMA_INTFCR = CH32RV_ADC_DMA_ALL;
}

/* Continuous with DMA, the ADC up and waiting (the DMA channel set up
 * already): the mode for ch32rv_adc_go(). */
CH32RV_ADC_RUN_STATIC uint32_t ch32rv_adc_arm(void)
{
    const uint32_t mode = ch32rv_adc_mode();
    ch32rv_adc_restart(mode);
    return mode | CH32RV_ADC_CTLR2_CONT | CH32RV_ADC_CTLR2_DMA;
}

/* The start: from the first pin of the sequence. */
CH32RV_ADC_RUN_STATIC void ch32rv_adc_go(uint32_t mode)
{
    CH32RV_ADC_CTLR2 = mode | CH32RV_ADC_CTLR2_ADON | CH32RV_ADC_CTLR2_SWSTART;
}

/* One round per trigger of the source `extsel` (the EXTSEL value of the
 * family's regular-group trigger, ch32rv_adc_trigger.h), the DMA channel set
 * up already: armed now, converting from the first trigger. The source is
 * switched with EXTTRIG on (analogRead() leaves it on): the caller starts its
 * timer, or its pad's EXTI line sees an edge, only after this. */
CH32RV_ADC_RUN_STATIC void ch32rv_adc_arm_triggered(uint8_t extsel)
{
    const uint32_t mode = ch32rv_adc_mode();
    ch32rv_adc_restart(mode);
    CH32RV_ADC_CTLR2 = (mode & ~CH32RV_ADC_EXTSEL_MASK) | CH32RV_ADC_EXTSEL(extsel) | CH32RV_ADC_CTLR2_DMA |
                       CH32RV_ADC_CTLR2_ADON;
}

/* A triggered run stopped: the trigger selection back to analogRead()'s
 * software start first (a pad's edge starts nothing more), then halted. */
CH32RV_ADC_RUN_STATIC void ch32rv_adc_halt_triggered(void)
{
    CH32RV_ADC_CTLR2 = (CH32RV_ADC_CTLR2 & ~(CH32RV_ADC_EXTSEL_MASK | CH32RV_ADC_CTLR2_DMA)) |
                       CH32RV_ADC_CTLR2_EXTSEL_SWSTART;
    ch32rv_adc_halt();
}

/* The DMA channel round `buffer` (`values` long, circular), its half and
 * full interrupts on. */
CH32RV_ADC_RUN_STATIC void ch32rv_adc_dma_ring(uint16_t *buffer, uint32_t values)
{
    CH32RV_ADC_DMA_MUX();
    CH32RV_ADC_DMA_PADDR = (uint32_t)(uintptr_t)&CH32RV_ADC_RDATAR;
    CH32RV_ADC_DMA_MADDR = (uint32_t)(uintptr_t)buffer;
    CH32RV_ADC_DMA_CNTR = values;
    CH32RV_ADC_DMA_CFGR = CH32RV_ADC_DMA_MINC | CH32RV_ADC_DMA_CIRC | CH32RV_ADC_DMA_16BIT | CH32RV_ADC_DMA_PL_HI |
                          CH32RV_ADC_DMA_HTIE | CH32RV_ADC_DMA_TCIE | CH32RV_ADC_DMA_EN;
}

/* Microseconds from the start to the end of n conversions, each moved by DMA
 * into the same place (no memory increment, so n is not bounded by a buffer).
 * 0 if the DMA never finished. */
CH32RV_ADC_RUN_STATIC uint32_t ch32rv_adc_timed_run(uint16_t n)
{
    static volatile uint16_t sink;
    ch32rv_adc_halt();
    CH32RV_ADC_DMA_PADDR = (uint32_t)(uintptr_t)&CH32RV_ADC_RDATAR;
    CH32RV_ADC_DMA_MADDR = (uint32_t)(uintptr_t)&sink;
    CH32RV_ADC_DMA_CNTR = n;
    CH32RV_ADC_DMA_CFGR = CH32RV_ADC_DMA_16BIT | CH32RV_ADC_DMA_PL_HI | CH32RV_ADC_DMA_EN;
    const uint32_t mode = ch32rv_adc_arm();
    const uint32_t t0 = micros();
    ch32rv_adc_go(mode);
    while ((CH32RV_ADC_DMA_INTFR & CH32RV_ADC_DMA_TCIF) == 0u) {
        if ((uint32_t)(micros() - t0) > 200000u) {
            ch32rv_adc_halt();
            return 0;
        }
    }
    const uint32_t dt = (uint32_t)(micros() - t0);
    ch32rv_adc_halt();
    CH32RV_ADC_DMA_DONE();
    return dt ? dt : 1u;
}

/* Give the ADC back to analogRead(): its single-conversion, software-started
 * set-up and longest sample times (wiring_analog.c). */
CH32RV_ADC_RUN_STATIC void ch32rv_adc_restore(void)
{
    CH32RV_ADC_CTLR1 = CH32RV_ADC_CTLR1_RESET;
    ch32rv_adc_restart(CH32RV_ADC_CTLR2_EXTSEL_SWSTART | CH32RV_ADC_CTLR2_EXTTRIG);
    CH32RV_ADC_SAMPTR1 = 0x00FFFFFFu;
    CH32RV_ADC_SAMPTR2 = 0x3FFFFFFFu;
    CH32RV_ADC_RSQR1 = 0;
    CH32RV_ADC_RSQR2 = 0;
}

#ifdef CH32RV_ADC_DUALMOD_SHIFT
/* ADC2 up for a dual-mode run (wiring_analog.c). */
void ch32rv_adc2_begin(void);
#endif

/* Links ch32rv_adc_run.c, whose vector calls the hooks below (an empty call:
 * it costs nothing once linked). */
void ch32rv_adc_run_link(void);
/* The DMA interrupt's hooks: CH32RVAdcStream's and analogContinuous()'s,
 * each a weak empty default in ch32rv_adc_run.c until its user is linked.
 * Each reads and clears the channel's flags itself, and does nothing while
 * its user is not running. */
void ch32rv_adc_stream_irq(void);
void ch32rv_adc_continuous_irq(void);

#ifdef __cplusplus
}
#endif
