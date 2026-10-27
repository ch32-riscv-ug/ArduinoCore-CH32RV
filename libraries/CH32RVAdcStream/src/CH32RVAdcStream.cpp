/* CH32RVAdcStream: ADC1 in continuous (and, with several pins, scan) mode,
 * its results moved by DMA1 channel 1 into one circular buffer whose halves
 * the half-transfer and transfer-complete interrupts hand over in turn.
 *
 * The ADC is the STM32F1-shaped one every family here has (ch32rv_registers.h);
 * DMA1 is at 0x40020000 with channel 1's registers from 0x08, as
 * CH32RVLogicCapture uses it. V407 / V467 take ADC1 on DMA1 channel 12
 * instead (EVT ADC_DMA; its flags are in DMA1_EXTEN at 0xF0, four bits per
 * channel from channel 9 - ch32v4x7_dma.h). H41x has no fixed request: DMAMUX
 * channel 1, which feeds DMA1 channel 1, is set to ADC1's request 120 (EVT
 * ADC_DMA writes 0x78; the register holds the number minus one). analogRead() brings the ADC up (clock divider,
 * calibration); this file only changes the sequence, the sample time and the
 * mode, and end() puts back what analogRead() expects. */
#include "CH32RVAdcStream.h"

#include "ch32rv_registers.h"

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

static CH32RVAdcStream *active;

static void adc_halt(void)
{
    const bool was_running = (CH32RV_ADC_CTLR2 & ADC_CTLR2_CONT) != 0u;
    CH32RV_ADC_CTLR2 &= ~(ADC_CTLR2_CONT | ADC_CTLR2_DMA);
    CH1_CFGR = 0;
    DMA_INTFCR = CH1_ALL;
    if (was_running) {
        /* A conversion in flight still ends; let it, so the next start begins clean. */
        for (uint32_t t0 = micros(); (CH32RV_ADC_STATR & CH32RV_ADC_STATR_EOC) == 0u;) {
            if ((uint32_t)(micros() - t0) > 1000u) {
                break;
            }
        }
    }
    (void)CH32RV_ADC_RDATAR;
}

bool CH32RVAdcStream::begin(pin_size_t pin, uint32_t rate)
{
    return begin(&pin, 1, rate);
}

bool CH32RVAdcStream::begin(const pin_size_t *pins, uint8_t count, uint32_t rate)
{
    if (!pins || count == 0 || count > MAX_PINS || rate == 0) {
        return false;
    }
    end();
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
        _channels[i] = (uint8_t)channel;
        /* analogRead() starts and calibrates the ADC and puts the pad in analog mode. */
        (void)analogRead(pins[i]);
    }
    _count = count;
    ch32rv_clock_enable(DMA1);
    MUX_ADC1();

    /* Fastest first: the first step at or below the rate is the one. */
    _step = 7;
    _rate = 0;
    for (uint8_t step = 0; step < 8; step++) {
        const uint32_t got = measure(step);
        if (got == 0) {
            continue;
        }
        if (got <= rate || step == 7) {
            _step = step;
            _rate = got;
            break;
        }
    }
    if (_rate == 0) {
        return false;
    }
    configure(_step);
    _begun = true;
    return true;
}

/* The sequence (the pins in order) and every one of their sample times at
 * `step`; single conversion mode, no DMA yet. */
bool CH32RVAdcStream::configure(uint8_t step)
{
    uint32_t smpr1 = CH32RV_ADC_SAMPTR1, smpr2 = CH32RV_ADC_SAMPTR2;
    uint32_t sq = 0;
    for (uint8_t i = 0; i < _count; i++) {
        const uint8_t c = _channels[i];
        if (c < 10u) {
            smpr2 = (smpr2 & ~(7u << (3u * c))) | ((uint32_t)step << (3u * c));
        } else {
            smpr1 = (smpr1 & ~(7u << (3u * (c - 10u)))) | ((uint32_t)step << (3u * (c - 10u)));
        }
        sq |= (uint32_t)c << (5u * i);
    }
    CH32RV_ADC_SAMPTR1 = smpr1;
    CH32RV_ADC_SAMPTR2 = smpr2;
    CH32RV_ADC_RSQR3 = sq;
    ADC_RSQR2 = 0;
    CH32RV_ADC_RSQR1 = (uint32_t)(_count - 1u) << 20;
    CH32RV_ADC_CTLR1 = _count > 1 ? ADC_CTLR1_SCAN : 0u;
    return true;
}

/* Rounds per second at `step`: 64 conversions moved by DMA into a scratch
 * array on the stack, timed with micros(). 0 if the DMA never finished. */
uint32_t CH32RVAdcStream::measure(uint8_t step)
{
    configure(step);
    uint16_t scratch[64];
    const uint16_t n = (uint16_t)(64u / _count * _count);
    adc_halt();
    CH1_PADDR = (uint32_t)(uintptr_t)&CH32RV_ADC_RDATAR;
    CH1_MADDR = (uint32_t)(uintptr_t)scratch;
    CH1_CNTR = n;
    CH1_CFGR = CFGR_MINC | CFGR_16BIT | CFGR_PL_HI | CFGR_EN;
    CH32RV_ADC_CTLR2 |= ADC_CTLR2_CONT | ADC_CTLR2_DMA;
    const uint32_t t0 = micros();
    CH32RV_ADC_CTLR2 |= CH32RV_ADC_CTLR2_SWSTART;
    uint32_t dt = 0;
    while ((DMA_INTFR & CH1_TCIF) == 0u) {
        dt = (uint32_t)(micros() - t0);
        if (dt > 200000u) {
            adc_halt();
            return 0;
        }
    }
    dt = (uint32_t)(micros() - t0);
    adc_halt();
    DMA_DONE();
    if (dt == 0) {
        dt = 1;
    }
    return (uint32_t)((uint64_t)n * 1000000u / dt / _count);
}

void CH32RVAdcStream::end()
{
    stop();
    if (!_begun) {
        return;
    }
    /* What analogRead() set up and keeps relying on (wiring_analog.c). */
    CH32RV_ADC_CTLR1 = 0;
    CH32RV_ADC_CTLR2 = CH32RV_ADC_CTLR2_EXTSEL_SWSTART | CH32RV_ADC_CTLR2_EXTTRIG | CH32RV_ADC_CTLR2_ADON;
    CH32RV_ADC_SAMPTR1 = 0x00FFFFFFu;
    CH32RV_ADC_SAMPTR2 = 0x3FFFFFFFu;
    CH32RV_ADC_RSQR1 = 0;
    ADC_RSQR2 = 0;
    _begun = false;
}

bool CH32RVAdcStream::startContinuous(uint16_t *buf0, uint16_t *buf1, uint16_t samples)
{
    if (buf1 != buf0 + samples) {
        return false;
    }
    return startContinuous(buf0, samples);
}

bool CH32RVAdcStream::startContinuous(uint16_t *buffer, uint16_t samples)
{
    if (!_begun || !buffer || samples < _count || samples % _count != 0 || samples > 32767u) {
        return false;
    }
    stop();
    _buf = buffer;
    _samples = samples;
    _ready = 0;
    _last = 0;
    _overruns = _filled = 0;
    active = this;
    configure(_step);
    adc_halt();
    MUX_ADC1();
    CH1_PADDR = (uint32_t)(uintptr_t)&CH32RV_ADC_RDATAR;
    CH1_MADDR = (uint32_t)(uintptr_t)buffer;
    CH1_CNTR = (uint32_t)samples * 2u;
    CH1_CFGR = CFGR_MINC | CFGR_CIRC | CFGR_16BIT | CFGR_PL_HI | CFGR_HTIE | CFGR_TCIE | CFGR_EN;
    _running = true;
    ch32rv_irq_enable(CH_IRQN);
    CH32RV_ADC_CTLR2 |= ADC_CTLR2_CONT | ADC_CTLR2_DMA;
    CH32RV_ADC_CTLR2 |= CH32RV_ADC_CTLR2_SWSTART;
    return true;
}

void CH32RVAdcStream::stop()
{
    if (!_running) {
        return;
    }
    ch32rv_irq_disable(CH_IRQN);
    adc_halt();
    _running = false;
}

const uint16_t *CH32RVAdcStream::readBuffer()
{
    noInterrupts();
    const uint8_t r = _ready;
    uint8_t take = 0xFF;
    if (r) {
        /* The older one first when both are waiting (the newer is then
         * being overwritten - already counted). */
        take = (r == 3u) ? (uint8_t)(_last ^ 1u) : (uint8_t)(r == 1u ? 0u : 1u);
        _ready = (uint8_t)(r & ~(1u << take));
    }
    interrupts();
    DMA_DONE();
    return take == 0xFF ? nullptr : _buf + (size_t)take * _samples;
}

void CH32RVAdcStream::isr()
{
    const uint32_t f = DMA_INTFR;
    DMA_INTFCR = f & CH1_ALL;
    /* Half-transfer: the first half is full; transfer-complete: the second.
     * Both at once (an interrupt held off for a whole half) - first, then second. */
    for (uint8_t half = 0; half < 2; half++) {
        if (!(f & (half == 0 ? CH1_HTIF : CH1_TCIF))) {
            continue;
        }
        if (_ready & (1u << half)) {
            _overruns++;                     /* the sketch still had not taken it */
        }
        _ready |= (uint8_t)(1u << half);
        _last = half;
        _filled++;
    }
}

extern "C" __attribute__((interrupt)) void CH_HANDLER(void)
{
    if (active) {
        active->isr();
    } else {
        DMA_INTFCR = CH1_ALL;
    }
}
