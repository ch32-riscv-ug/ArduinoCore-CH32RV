/* arduino-esp32's ADC continuous mode (analogContinuous() and the rest,
 * Arduino.h) on ADC1: a timer of the family's trigger sources
 * (internal/ch32rv_adc_trigger.h) starts one round of the pins at the
 * sampling frequency's pace, the DMA moves the results into two frames
 * (internal/ch32rv_adc_run.h), and analogContinuousRead() averages the latest
 * whole frame per pin, as arduino-esp32's does.
 *
 * Where ESP-IDF picks the ADC's own pattern clock, this ADC has none: the
 * pace is the timer's, so it is the frequency asked for within the timer's
 * divisions of its clock (exact where they divide). The sample time is the
 * longest whose round still fits in 80 % of a trigger's period (measured once
 * per step in analogContinuous(): the steps' times are not in a table),
 * the shortest when none does - the ADC then misses triggers, as asked.
 * Only what cannot run is refused: no pins, a pin no ADC1 input, a frame the
 * DMA cannot count (more than 32767 values), a pace no timer reaches. */
#include "Arduino.h"
#include <stdlib.h>
#include "internal/ch32rv_adc_run.h"
#include "internal/ch32rv_adc_trigger.h"

#define MAX_CHANNELS 16u              /* the regular sequence's length */

static struct {
    bool init, running;
    uint8_t count, step, width;   /* width 0: the ADC's own */
    uint8_t channels[MAX_CHANNELS];
    uint32_t frame;               /* values in a frame (conversions_per_pin x pins) */
    uint32_t vdd_mv;
    uint16_t *buf;                /* two frames */
    adc_continuous_result_t *result;
    ch32rv_adc_timer_trigger_t timer;
    void (*fn)(void);
    volatile uint8_t ready;       /* bit n: frame n full and not yet read */
    volatile uint8_t last;        /* the frame filled most recently */
} cont;

/* The DMA's interrupt (ch32rv_adc_run.c), as each frame fills: nothing
 * while no run is going (CH32RVAdcStream's hook, after this one, has the
 * flags then). */
void ch32rv_adc_continuous_irq(void)
{
    if (!cont.running) {
        return;
    }
    const uint32_t f = CH32RV_ADC_DMA_INTFR;
    CH32RV_ADC_DMA_INTFCR = f & CH32RV_ADC_DMA_ALL;
    CH32RV_ADC_DMA_DONE();
    for (uint8_t half = 0; half < 2; half++) {
        if (f & (half == 0 ? CH32RV_ADC_DMA_HTIF : CH32RV_ADC_DMA_TCIF)) {
            cont.ready |= (uint8_t)(1u << half);
            cont.last = half;
            if (cont.fn) {
                cont.fn();
            }
        }
    }
}

/* The pins' ADC1 channels: false when one is no ADC1 input (X305 / X315: an
 * ADC2..4 pad, whose requests reach no DMA channel here). Each pad set up as
 * analogRead() does it (analog mode, the ADC started and calibrated). */
static bool channels_of(const uint8_t *pins, uint8_t count)
{
#ifdef NUM_ANALOG_INPUTS
    for (uint8_t i = 0; i < count; i++) {
        const pin_size_t pad = CH32RV_PIN_RESOLVE(pins[i]);
        const unsigned channel = CH32RV_PIN_TO_ADC_CHANNEL(pad);
        if (channel == NOT_AN_ANALOG_PIN) {
            return false;
        }
#ifdef CH32RV_PIN_TO_ADC_INSTANCE
        if (CH32RV_PIN_TO_ADC_INSTANCE(pad) != 1) {
            return false;
        }
#endif
        cont.channels[i] = (uint8_t)channel;
        (void)analogRead(pins[i]);
    }
    ch32rv_clock_enable(DMA1);
    CH32RV_ADC_DMA_MUX();
    return true;
#else
    (void)pins;
    (void)count;
    return false;
#endif
}

/* The sequence (up to 16 places: RSQR3, RSQR2, RSQR1 at descending
 * addresses, six 5-bit places each, RSQR1 four and the length at bit 20)
 * and every channel's sample time at `step`, single conversion. */
static void sequence(uint8_t count, uint8_t step)
{
    uint32_t smpr1 = CH32RV_ADC_SAMPTR1, smpr2 = CH32RV_ADC_SAMPTR2;
    CH32RV_ADC_RSQR1 = (uint32_t)(count - 1u) << 20;
    CH32RV_ADC_RSQR2 = 0;
    CH32RV_ADC_RSQR3 = 0;
    volatile uint32_t *sq = &CH32RV_ADC_RSQR3;
    uint8_t place = 0;
    for (uint8_t i = 0; i < count; i++) {
        const uint8_t c = cont.channels[i];
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
    CH32RV_ADC_CTLR1 = count > 1u ? CH32RV_ADC_CTLR1_SCAN : 0u;
}

/* Rounds per second free-running at `step`, from two runs (what both pay
 * once drops out, as CH32RVAdcStream measures). 0 if the DMA never finished. */
static uint32_t measure(uint8_t count, uint8_t step)
{
    sequence(count, step);
    const uint16_t n1 = (uint16_t)(32u / count * count);
    const uint16_t n2 = (uint16_t)(544u / count * count);
    const uint32_t dt1 = ch32rv_adc_timed_run(n1);
    const uint32_t dt2 = dt1 ? ch32rv_adc_timed_run(n2) : 0u;
    if (dt2 <= dt1) {
        return 0;
    }
    return (uint32_t)((uint64_t)(n2 - n1) * 1000000u / (dt2 - dt1) / count);
}

bool analogContinuous(const uint8_t pins[], size_t pins_count, uint32_t conversions_per_pin,
                      uint32_t sampling_freq_hz, void (*userFunc)(void))
{
#if !CH32RV_ADC_CLOCK_OK
    (void)pins;
    (void)pins_count;
    (void)conversions_per_pin;
    (void)sampling_freq_hz;
    (void)userFunc;
    return false;
#else
    if (cont.init || !pins || pins_count == 0u || pins_count > MAX_CHANNELS ||
        conversions_per_pin == 0u || sampling_freq_hz == 0u ||
        (uint64_t)conversions_per_pin * pins_count > 32767u) {
        return false;
    }
    const uint8_t count = (uint8_t)pins_count;
    if (!channels_of(pins, count)) {
        return false;
    }
    /* One round of the pins per trigger: sampling_freq_hz conversions a
     * second, shared by the pins as ESP-IDF's pattern shares them. */
    if (!ch32rv_adc_timer_trigger_begin(&cont.timer, sampling_freq_hz, count)) {
        return false;
    }
    const uint32_t frame = conversions_per_pin * count;
    cont.buf = (uint16_t *)malloc(2u * frame * sizeof(uint16_t));
    cont.result = (adc_continuous_result_t *)malloc(count * sizeof(adc_continuous_result_t));
    if (!cont.buf || !cont.result) {
        free(cont.buf);
        free(cont.result);
        cont.buf = 0;
        cont.result = 0;
        ch32rv_adc_timer_trigger_end(&cont.timer);
        return false;
    }
    for (uint8_t k = 0; k < count; k++) {
        cont.result[k].pin = pins[k];
        cont.result[k].channel = cont.channels[k];
        cont.result[k].avg_read_raw = 0;
        cont.result[k].avg_read_mvolts = 0;
    }
    /* The supply, for the millivolts: measured now, while analogRead()'s
     * set-up is still the ADC's. */
    cont.vdd_mv = ch32rv_adc_vdd_millivolts();
    if (cont.vdd_mv == 0u) {
        cont.vdd_mv = 3300u;
    }
    /* Rounds a second the pace asks for, x 1.25: the longest step that
     * converts a round in 80 % of the period. */
    const uint64_t want = ((uint64_t)ch32rv_adc_timer_trigger_millihz(&cont.timer) * 5u + 3999u) / 4000u;
    cont.step = 0;
    for (int8_t step = 7; step >= 0; step--) {
        if (measure(count, (uint8_t)step) >= want) {
            cont.step = (uint8_t)step;
            break;
        }
    }
    sequence(count, cont.step);
    cont.count = count;
    cont.frame = frame;
    cont.fn = userFunc;
    cont.ready = 0;
    cont.init = true;
    return true;
#endif
}

bool analogContinuousRead(adc_continuous_result_t **buffer, uint32_t timeout_ms)
{
    if (!cont.init) {
        if (buffer) {
            *buffer = 0;
        }
        return false;
    }
    const uint32_t start = millis();
    while (cont.ready == 0u) {
        if ((uint32_t)(millis() - start) >= timeout_ms) {
            if (buffer) {
                *buffer = 0;
            }
            return false;
        }
    }
    noInterrupts();
    const uint8_t half = cont.last;   /* the newest whole frame */
    cont.ready = 0;
    interrupts();
    __asm__ volatile("fence rw, rw" ::: "memory");
    const uint16_t *v = cont.buf + (size_t)half * cont.frame;
    const uint32_t full = (1u << CH32RV_ADC_BITS) - 1u;
    for (uint8_t k = 0; k < cont.count; k++) {
        uint32_t sum = 0, n = 0;
        for (uint32_t i = k; i < cont.frame; i += cont.count, n++) {
            sum += v[i];
        }
        const uint32_t avg = n ? (sum + n / 2u) / n : 0u;
        cont.result[k].avg_read_mvolts = (int)((avg * cont.vdd_mv + full / 2u) / full);
        const uint8_t width = cont.width ? cont.width : CH32RV_ADC_BITS;
        cont.result[k].avg_read_raw = (int)(width >= CH32RV_ADC_BITS ? avg << (width - CH32RV_ADC_BITS)
                                                                   : avg >> (CH32RV_ADC_BITS - width));
    }
    if (buffer) {
        *buffer = cont.result;
    }
    return true;
}

bool analogContinuousStart(void)
{
    if (!cont.init) {
        return false;
    }
    if (cont.running) {
        return true;
    }
    cont.ready = 0;
    sequence(cont.count, cont.step);
    ch32rv_adc_run_link();
    ch32rv_irq_disable(CH32RV_ADC_DMA_IRQN);
    ch32rv_adc_halt();
    ch32rv_adc_dma_ring(cont.buf, 2u * cont.frame);
    cont.running = true;
    ch32rv_irq_enable(CH32RV_ADC_DMA_IRQN);
    ch32rv_adc_arm_triggered(cont.timer.extsel);
    ch32rv_adc_timer_trigger_start(&cont.timer);
    return true;
}

bool analogContinuousStop(void)
{
    if (!cont.init) {
        return false;
    }
    if (cont.running) {
        ch32rv_adc_timer_trigger_stop(&cont.timer);
        ch32rv_irq_disable(CH32RV_ADC_DMA_IRQN);
        ch32rv_adc_halt_triggered();
        cont.running = false;
    }
    return true;
}

bool analogContinuousDeinit(void)
{
    if (!cont.init) {
        return true;
    }
    analogContinuousStop();
    ch32rv_adc_timer_trigger_end(&cont.timer);
    ch32rv_adc_restore();
    free(cont.buf);
    free(cont.result);
    cont.buf = 0;
    cont.result = 0;
    cont.init = false;
    return true;
}

void analogContinuousSetAtten(adc_attenuation_t attenuation)
{
    /* The ADC measures 0 .. VDD on every pad; there is no attenuator. Kept
     * so a sketch written for an ESP32 builds, as analogReference(). */
    (void)attenuation;
}

void analogContinuousSetWidth(uint8_t bits)
{
    if (bits >= 1u && bits <= 16u) {
        cont.width = bits;
    }
}
