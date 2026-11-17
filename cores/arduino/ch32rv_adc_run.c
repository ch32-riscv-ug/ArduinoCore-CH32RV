/* ADC1 runs moved by DMA (internal/ch32rv_adc_run.h): ADC1 in single or
 * scan mode, continuous or one round per external trigger, its results moved
 * by DMA into one circular buffer whose halves the half-transfer and
 * transfer-complete interrupts hand over in turn. CH32RVAdcStream and
 * analogContinuous() run on it.
 *
 * The ADC is the STM32F1-shaped one every family here has (ch32rv_registers.h);
 * DMA1 is at 0x40020000 with channel 1's registers from 0x08, as
 * CH32RVLogicCapture uses it. V407 / V467 take ADC1 on DMA1 channel 12
 * instead (EVT ADC_DMA; its flags are in DMA1_EXTEN at 0xF0, four bits per
 * channel from channel 9 - ch32v4x7_dma.h). H41x has no fixed request: DMAMUX
 * channel 1, which feeds DMA1 channel 1, is set to ADC1's request 120 (EVT
 * ADC_DMA writes 0x78; the register holds the number minus one).
 * analogRead() brings the ADC up (clock divider, calibration); this file only
 * changes the sequence, the sample time and the mode, and
 * ch32rv_adc_run_restore() puts back what analogRead() expects. */
#include "Arduino.h"
#include "internal/ch32rv_adc_run.h"
#include "internal/ch32rv_adc_trigger.h"
#include "internal/ch32rv_registers.h"

#define DMA1_BASE   0x40020000u
#if defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
#define DMA_INTFR   CH32RV_REG32(DMA1_BASE + 0xF0u)
#define DMA_INTFCR  CH32RV_REG32(DMA1_BASE + 0xF4u)
#define CH_BASE     (DMA1_BASE + 0xD0u)
#define CH_SHIFT    16u
#define CH_IRQN     CH32RV_IRQN_DMA1_Channel12
#define CH_HANDLER  DMA1_Channel12_IRQHandler
#else   /* every other family, and H41x through the DMAMUX */
#define DMA_INTFR   CH32RV_REG32(DMA1_BASE + 0x00u)
#define DMA_INTFCR  CH32RV_REG32(DMA1_BASE + 0x04u)
#define CH_BASE     (DMA1_BASE + 0x08u)
#define CH_SHIFT    0u
#define CH_IRQN     CH32RV_IRQN_DMA1_Channel1
#define CH_HANDLER  DMA1_Channel1_IRQHandler
#endif
#if defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
#define DMAMUX_C0_3 CH32RV_REG32(0x40020800u)   /* mux channels 1..4, a byte each; channel 1 -> DMA1 channel 1 */
#define REQ_ADC1    120u
#define MUX_ADC1()  (DMAMUX_C0_3 = (DMAMUX_C0_3 & ~0x7Fu) | (REQ_ADC1 - 1u))
#else
#define MUX_ADC1()  ((void)0)
#endif
#define CH1_CFGR    CH32RV_REG32(CH_BASE + 0x00u)
#define CH1_CNTR    CH32RV_REG32(CH_BASE + 0x04u)
#define CH1_PADDR   CH32RV_REG32(CH_BASE + 0x08u)
#define CH1_MADDR   CH32RV_REG32(CH_BASE + 0x0Cu)

/* INTFR / INTFCR, the channel's four bits. */
#define CH1_GIF   (1u << (CH_SHIFT + 0u))
#define CH1_TCIF  (1u << (CH_SHIFT + 1u))
#define CH1_HTIF  (1u << (CH_SHIFT + 2u))
#define CH1_TEIF  (1u << (CH_SHIFT + 3u))
#define CH1_ALL   (CH1_GIF | CH1_TCIF | CH1_HTIF | CH1_TEIF)

/* CFGR */
#define CFGR_EN     (1u << 0)
#define CFGR_TCIE   (1u << 1)
#define CFGR_HTIE   (1u << 2)
#define CFGR_CIRC   (1u << 5)
#define CFGR_MINC   (1u << 7)
#define CFGR_16BIT  ((1u << 8) | (1u << 10))   /* PSIZE and MSIZE 16 bits */
#define CFGR_PL_HI  (2u << 12)

#define ADC_CTLR1_SCAN  (1u << 8)
#define ADC_CTLR2_CONT  (1u << 1)
#define ADC_CTLR2_DMA   (1u << 8)
#define ADC_RSQR2       CH32RV_REG32(CH32RV_ADC1_BASE + 0x30u)

#define DMA_DONE() __asm__ volatile("fence rw, rw" ::: "memory")

static ch32rv_adc_run_handler_t run_handler;
static void *run_context;

/* Stopping by clearing CONT lets the conversion in flight, and with several
 * pins the rest of the sequence, finish after the stop: a result left behind,
 * its EOC set, which analogRead() after end() returned at once instead of its
 * own pin (the stream's last pin read; CH32X035, 2026-11-05). So a stop powers
 * the ADC down (ADON 0 drops whatever is converting) and up again (ADON 0 to 1
 * starts nothing: measured, no conversion follows without SWSTART), the mode
 * is set while it waits, and a start is the one write that sets SWSTART (or,
 * triggered, the one that sets EXTTRIG). */
static void adc_restart(uint32_t mode)
{
    CH32RV_ADC_CTLR2 = mode;
    CH32RV_ADC_CTLR2 = mode | CH32RV_ADC_CTLR2_ADON;
    for (volatile int settle = 0; settle < 1000; settle++) {   /* as analogRead()'s power-up */
    }
    (void)CH32RV_ADC_RDATAR;
}

/* What CTLR2 holds besides the bits a run sets (the trigger selection -
 * analogRead()'s software start, put back by ch32rv_adc_run_stop_triggered()
 * - and a temperature sensor enable). */
static uint32_t adc_mode(void)
{
    return CH32RV_ADC_CTLR2 & ~(CH32RV_ADC_CTLR2_ADON | ADC_CTLR2_CONT | ADC_CTLR2_DMA | CH32RV_ADC_CTLR2_SWSTART);
}

/* Stopped, powered down and up (adc_restart), with no DMA request left over.
 * A conversion the DMA never took - the ADC still converting when a counted
 * transfer (measure) ran out - leaves a request that outlives the power-down
 * and a read of RDATAR: the next channel enable moved one stale value at once,
 * before the ADC started, and every pin after it sat one place late (pin 0's
 * place holding the last pin's - X A B C A B C, CH32X035 / V003 with two or
 * three pins, 2026-11-07). It is taken here into a dummy, with the ADC's DMA
 * bit set so the ADC sees it answered; with none pending nothing moves. */
static void adc_halt(void)
{
    const uint32_t mode = adc_mode();
    adc_restart(mode);
    static volatile uint16_t dummy;
    CH1_CFGR = 0;
    CH32RV_ADC_CTLR2 = mode | CH32RV_ADC_CTLR2_ADON | ADC_CTLR2_DMA;
    CH1_PADDR = (uint32_t)(uintptr_t)&CH32RV_ADC_RDATAR;
    CH1_MADDR = (uint32_t)(uintptr_t)&dummy;
    CH1_CNTR = 1;
    CH1_CFGR = CFGR_16BIT | CFGR_PL_HI | CFGR_EN;
    for (volatile int settle = 0; settle < 100 && CH1_CNTR; settle++) {
    }
    CH1_CFGR = 0;
    CH32RV_ADC_CTLR2 = mode | CH32RV_ADC_CTLR2_ADON;
    DMA_INTFCR = CH1_ALL;
}

/* Continuous with DMA, the ADC up and waiting (the DMA channel set up
 * already): the mode for adc_go(). */
static uint32_t adc_arm(void)
{
    const uint32_t mode = adc_mode();
    adc_restart(mode);
    return mode | ADC_CTLR2_CONT | ADC_CTLR2_DMA;
}

/* The start: from the first pin of the sequence. */
static void adc_go(uint32_t mode)
{
    CH32RV_ADC_CTLR2 = mode | CH32RV_ADC_CTLR2_ADON | CH32RV_ADC_CTLR2_SWSTART;
}

bool ch32rv_adc_run_channels(const uint8_t *pins, uint8_t count, uint8_t *channels)
{
    if (!pins || count == 0u || count > CH32RV_ADC_RUN_MAX_CHANNELS) {
        return false;
    }
#ifdef NUM_ANALOG_INPUTS
    for (uint8_t i = 0; i < count; i++) {
        const pin_size_t pad = CH32RV_PIN_RESOLVE(pins[i]);
        const unsigned channel = CH32RV_PIN_TO_ADC_CHANNEL(pad);
        if (channel == NOT_AN_ANALOG_PIN) {
            return false;
        }
#ifdef CH32RV_PIN_TO_ADC_INSTANCE
        if (CH32RV_PIN_TO_ADC_INSTANCE(pad) != 1) {
            return false;                    /* X305 / X315: only ADC1 has this DMA request */
        }
#endif
        channels[i] = (uint8_t)channel;
        /* analogRead() starts and calibrates the ADC and puts the pad in analog mode. */
        (void)analogRead(pins[i]);
    }
    ch32rv_clock_enable(DMA1);
    MUX_ADC1();
    return true;
#else
    (void)channels;
    return false;
#endif
}

void ch32rv_adc_run_sequence(const uint8_t *channels, uint8_t count, uint8_t step)
{
    uint32_t smpr1 = CH32RV_ADC_SAMPTR1, smpr2 = CH32RV_ADC_SAMPTR2;
    /* RSQR3, RSQR2, RSQR1 (descending addresses): six 5-bit places each
     * (RSQR1 four, and the length at bit 20). */
    CH32RV_ADC_RSQR1 = (uint32_t)(count - 1u) << 20;
    ADC_RSQR2 = 0;
    CH32RV_ADC_RSQR3 = 0;
    volatile uint32_t *sq = &CH32RV_ADC_RSQR3;
    uint8_t place = 0;
    for (uint8_t i = 0; i < count; i++) {
        const uint8_t c = channels[i];
        if (c < 10u) {
            smpr2 = (smpr2 & ~(7u << (3u * c))) | ((uint32_t)step << (3u * c));
        } else {
            smpr1 = (smpr1 & ~(7u << (3u * (c - 10u)))) | ((uint32_t)step << (3u * (c - 10u)));
        }
        *sq |= (uint32_t)c << place;
        place += 5u;
        if (place == 30u) {
            place = 0;
            sq--;
        }
    }
    CH32RV_ADC_SAMPTR1 = smpr1;
    CH32RV_ADC_SAMPTR2 = smpr2;
    CH32RV_ADC_CTLR1 = count > 1u ? ADC_CTLR1_SCAN : 0u;
}

/* Microseconds from the start to the end of n conversions, each moved by DMA
 * into the same place (no memory increment, so n is not bounded by a buffer).
 * 0 if the DMA never finished. */
static uint32_t timed_run(uint16_t n)
{
    static volatile uint16_t sink;
    adc_halt();
    CH1_PADDR = (uint32_t)(uintptr_t)&CH32RV_ADC_RDATAR;
    CH1_MADDR = (uint32_t)(uintptr_t)&sink;
    CH1_CNTR = n;
    CH1_CFGR = CFGR_16BIT | CFGR_PL_HI | CFGR_EN;
    const uint32_t mode = adc_arm();
    const uint32_t t0 = micros();
    adc_go(mode);
    while ((DMA_INTFR & CH1_TCIF) == 0u) {
        if ((uint32_t)(micros() - t0) > 200000u) {
            adc_halt();
            return 0;
        }
    }
    const uint32_t dt = (uint32_t)(micros() - t0);
    adc_halt();
    DMA_DONE();
    return dt ? dt : 1u;
}

/* Rounds per second at `step`, from two runs: the rate is the difference in
 * conversions over the difference in time, so what both runs pay once - the
 * ADC's first conversion after the start, the calls around it - drops out.
 * One run of 64 conversions counted that in and came out 4 % slow on a
 * CH32X035 (960 Hz for a 1 kHz wave, 2026-11-12). */
uint32_t ch32rv_adc_run_measure(const uint8_t *channels, uint8_t count, uint8_t step)
{
    ch32rv_adc_run_sequence(channels, count, step);
    const uint16_t n1 = (uint16_t)(32u / count * count);
    const uint16_t n2 = (uint16_t)(544u / count * count);
    const uint32_t dt1 = timed_run(n1);
    const uint32_t dt2 = dt1 ? timed_run(n2) : 0u;
    if (dt2 <= dt1) {
        return 0;
    }
    return (uint32_t)((uint64_t)(n2 - n1) * 1000000u / (dt2 - dt1) / count);
}

/* The DMA channel round the buffer, its interrupts on. */
static void dma_ring(uint16_t *buffer, uint32_t values, ch32rv_adc_run_handler_t handler, void *context)
{
    ch32rv_adc_run_stop();
    MUX_ADC1();
    run_handler = handler;
    run_context = context;
    CH1_PADDR = (uint32_t)(uintptr_t)&CH32RV_ADC_RDATAR;
    CH1_MADDR = (uint32_t)(uintptr_t)buffer;
    CH1_CNTR = values;
    CH1_CFGR = CFGR_MINC | CFGR_CIRC | CFGR_16BIT | CFGR_PL_HI | CFGR_HTIE | CFGR_TCIE | CFGR_EN;
    ch32rv_irq_enable(CH_IRQN);
}

void ch32rv_adc_run_start(uint16_t *buffer, uint32_t values, ch32rv_adc_run_handler_t handler, void *context)
{
    dma_ring(buffer, values, handler, context);
    adc_go(adc_arm());
}

void ch32rv_adc_run_start_triggered(uint16_t *buffer, uint32_t values, uint8_t extsel,
                                    ch32rv_adc_run_handler_t handler, void *context)
{
    dma_ring(buffer, values, handler, context);
    /* The source switched with EXTTRIG on (analogRead() leaves it on): the
     * caller starts its timer, or its pad's EXTI line sees an edge, only
     * after this, so nothing converts before the first trigger. */
    CH32RV_ADC_CTLR2 = (adc_mode() & ~CH32RV_ADC_EXTSEL_MASK) | CH32RV_ADC_EXTSEL(extsel) |
                       ADC_CTLR2_DMA | CH32RV_ADC_CTLR2_ADON;
}

void ch32rv_adc_run_stop_triggered(void)
{
    /* The software start again first: the source (a pad's edge) starts
     * nothing more while the ADC goes down and up. */
    CH32RV_ADC_CTLR2 = (CH32RV_ADC_CTLR2 & ~(CH32RV_ADC_EXTSEL_MASK | ADC_CTLR2_DMA)) |
                       CH32RV_ADC_CTLR2_EXTSEL_SWSTART;
    ch32rv_adc_run_stop();
}

void ch32rv_adc_run_stop(void)
{
    ch32rv_irq_disable(CH_IRQN);
    adc_halt();
}

uint32_t ch32rv_adc_run_remaining(void)
{
    return CH1_CNTR;
}

uint32_t ch32rv_adc_run_pending(void)
{
    return (DMA_INTFR >> (CH_SHIFT + 1u)) & 3u;
}

void ch32rv_adc_run_restore(void)
{
    /* What analogRead() set up and keeps relying on (wiring_analog.c). */
    CH32RV_ADC_CTLR1 = 0;
    adc_restart(CH32RV_ADC_CTLR2_EXTSEL_SWSTART | CH32RV_ADC_CTLR2_EXTTRIG);
    CH32RV_ADC_SAMPTR1 = 0x00FFFFFFu;
    CH32RV_ADC_SAMPTR2 = 0x3FFFFFFFu;
    CH32RV_ADC_RSQR1 = 0;
    ADC_RSQR2 = 0;
}

CH32RV_INTERRUPT void CH_HANDLER(void)
{
    const uint32_t f = DMA_INTFR;
    DMA_INTFCR = f & CH1_ALL;
    DMA_DONE();
    /* Half-transfer: the first half is full; transfer-complete: the second.
     * Both at once (an interrupt held off for a whole half) - both flags. */
    if (run_handler) {
        run_handler(run_context, (f >> (CH_SHIFT + 1u)) & 3u);
    }
}
