/* CH32RVAdcStream: ADC1 in continuous (and, with several pins, scan) mode,
 * its results moved by DMA1 channel 1 into one circular buffer whose halves
 * the half-transfer and transfer-complete interrupts hand over in turn. The
 * DMA channel, its interrupt and the ADC's stops are the core's
 * (internal/ch32rv_adc_run.h); the triggered runs are
 * CH32RVAdcStreamTrigger.cpp's.
 *
 * The ADC is the STM32F1-shaped one every family here has (ch32rv_registers.h).
 * analogRead() brings the ADC up (clock divider, calibration); this file only
 * changes the sequence, the sample time and the mode, and end() puts back
 * what analogRead() expects. */
#include "CH32RVAdcStream.h"

#include "internal/ch32rv_adc_run.h"

static CH32RVAdcStream *active;

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
    if (!ch32rv_adc_dma_take()) {
        return false;
    }

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
        ch32rv_adc_dma_give();
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
    CH32RV_ADC_RSQR2 = 0;
    CH32RV_ADC_RSQR1 = (uint32_t)(_count - 1u) << 20;
    CH32RV_ADC_CTLR1 = (_count > 1 ? CH32RV_ADC_CTLR1_SCAN : 0u) | CH32RV_ADC_CTLR1_RESET;
    return true;
}

/* Rounds per second at `step`, from two runs: the rate is the difference in
 * conversions over the difference in time, so what both runs pay once - the
 * ADC's first conversion after the start, the calls around it - drops out.
 * One run of 64 conversions counted that in and came out 4 % slow on a
 * CH32X035 (960 Hz for a 1 kHz wave, 2026-11-12). 0 if the DMA never finished. */
uint32_t CH32RVAdcStream::measure(uint8_t step)
{
    configure(step);
    const uint16_t n1 = (uint16_t)(32u / _count * _count);
    const uint16_t n2 = (uint16_t)(544u / _count * _count);
    const uint32_t dt1 = ch32rv_adc_timed_run(n1);
    const uint32_t dt2 = dt1 ? ch32rv_adc_timed_run(n2) : 0u;
    if (dt2 <= dt1) {
        return 0;
    }
    return (uint32_t)((uint64_t)(n2 - n1) * 1000000u / (dt2 - dt1) / _count);
}

void CH32RVAdcStream::end()
{
    stop();
    triggerControl(TRIGGER_END);
    if (!_begun) {
        return;
    }
    ch32rv_adc_restore();
    ch32rv_adc_dma_give();
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
    ch32rv_adc_run_link();
    configure(_step);
    ch32rv_adc_halt();
    CH32RV_ADC_DMA_MUX();
    CH32RV_ADC_DMA_PADDR = (uint32_t)(uintptr_t)&CH32RV_ADC_RDATAR;
    CH32RV_ADC_DMA_MADDR = (uint32_t)(uintptr_t)buffer;
    CH32RV_ADC_DMA_CNTR = (uint32_t)samples * 2u;
    CH32RV_ADC_DMA_CFGR = CH32RV_ADC_DMA_MINC | CH32RV_ADC_DMA_CIRC | CH32RV_ADC_DMA_16BIT | CH32RV_ADC_DMA_PL_HI | CH32RV_ADC_DMA_HTIE | CH32RV_ADC_DMA_TCIE | CH32RV_ADC_DMA_EN;
    _running = true;
    ch32rv_irq_enable(CH32RV_ADC_DMA_IRQN);
    if (!triggerControl(TRIGGER_START)) {
        ch32rv_adc_go(ch32rv_adc_arm());
    }
    return true;
}

void CH32RVAdcStream::stop()
{
    if (!_running) {
        return;
    }
    ch32rv_irq_disable(CH32RV_ADC_DMA_IRQN);
    if (!triggerControl(TRIGGER_STOP)) {
        ch32rv_adc_halt();
    }
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
    CH32RV_ADC_DMA_DONE();
    return take == 0xFF ? nullptr : _buf + (size_t)take * _samples;
}

void CH32RVAdcStream::isr()
{
    const uint32_t f = CH32RV_ADC_DMA_INTFR;
    CH32RV_ADC_DMA_INTFCR = f & CH32RV_ADC_DMA_ALL;
    /* Half-transfer: the first half is full; transfer-complete: the second.
     * Both at once (an interrupt held off for a whole half) - first, then second. */
    for (uint8_t half = 0; half < 2; half++) {
        if (!(f & (half == 0 ? CH32RV_ADC_DMA_HTIF : CH32RV_ADC_DMA_TCIF))) {
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

uint32_t CH32RVAdcStream::conversions() const
{
    if (!_running || !_count) {
        return 0;
    }
    const uint32_t total = (uint32_t)_samples * 2u;
    uint32_t laps, remaining, pending;
    /* The counter and the wrap's flag read together: a wrap between them
     * would count its lap twice or not at all. */
    for (;;) {
        noInterrupts();
        pending = CH32RV_ADC_DMA_INTFR & CH32RV_ADC_DMA_TCIF;
        laps = _filled / 2u;             /* halves alternate: every second one ends a lap */
        remaining = CH32RV_ADC_DMA_CNTR;
        const uint32_t again = CH32RV_ADC_DMA_INTFR & CH32RV_ADC_DMA_TCIF;
        interrupts();
        if (again == pending) {
            break;
        }
    }
    if (pending) {
        laps++;                          /* wrapped, its interrupt still to run */
    }
    const uint32_t written = remaining >= total ? 0u : total - remaining;
    return (laps * total + written) / _count;
}

/* No trigger linked: nothing to do (CH32RVAdcStreamTrigger.cpp has the real
 * one; the library is an archive, so it comes only with beginTimer() /
 * beginExternal()). */
__attribute__((weak)) bool CH32RVAdcStream::triggerControl(uint8_t op)
{
    (void)op;
    return false;
}

extern "C" void ch32rv_adc_stream_irq(void)
{
    if (active) {
        active->isr();
    } else {
        CH32RV_ADC_DMA_INTFCR = CH32RV_ADC_DMA_ALL;
    }
}
