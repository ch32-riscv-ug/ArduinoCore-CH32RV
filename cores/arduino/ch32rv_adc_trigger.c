/* ADC1 trigger sources (internal/ch32rv_adc_trigger.h): a timer of the
 * family's list, held whole through the timer manager (CH32RVTimer.h) like
 * timerBegin()'s, or a pad's EXTI line in event mode.
 *
 * A timer source is either its TRGO, set to the update event (CTLR2.MMS
 * 010), or a channel's compare: PWM mode 1 at half the period, the channel
 * enabled - the ADC takes the channel's output signal (OCx), which an
 * advanced timer gates with BDTR.MOE, so MOE is set (the EVTs' TIM_Trigger do
 * both). The pad stays an input, so nothing leaves the chip. */
#include "Arduino.h"
#include "CH32RVTimer.h"
#include "internal/ch32rv_adc_trigger.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

#define TIM_CTLR2(b)      CH32RV_REG16((b) + 0x04u)
#define TIM_CTLR2_MMS_UP  (2u << 4)          /* TRGO: the update event */
#define TIM_OCMODE_PWM1   0x60u              /* OCxM 110, no preload */

static const CH32RVTimerOwner trigger_owner = {&trigger_owner, 0, 0};
static CH32RVTimerLease trigger_lease;

static const struct {
    uint8_t timer, event, extsel;
} timer_sources[] = {
#define SOURCE(timer, event, extsel) {(timer), (event), (extsel)},
    CH32RV_ADC_TIMER_SOURCES(SOURCE)
#undef SOURCE
};

/* prescaler x period = the timer's clock x per / hz, each within 1..2^16
 * (the period up to 2^32 on a 32-bit timer); at least 2 for a compare,
 * which needs a low and a high part. */
static bool divisions(uint32_t clock, uint32_t hz, uint32_t per, uint8_t bits,
                      bool compare, uint32_t *prescaler, uint32_t *period)
{
    const uint64_t ticks = ((uint64_t)clock * per + hz / 2u) / hz;
    const uint64_t most = bits == 32u ? 0x100000000ull : 0x10000ull;
    if (ticks < (compare ? 2u : 1u)) {
        return false;
    }
    const uint64_t psc = (ticks + most - 1u) / most;
    if (psc > 0x10000u) {
        return false;
    }
    *prescaler = (uint32_t)psc;
    *period = (uint32_t)((ticks + psc / 2u) / psc);
    if (*period > most) {
        *period = (uint32_t)most;
    }
    return !(compare && *period < 2u);
}

bool ch32rv_adc_timer_trigger_begin(ch32rv_adc_timer_trigger_t *t, uint32_t hz, uint32_t per)
{
    t->timer = 0;
    if (hz == 0u || per == 0u) {
        return false;
    }
    for (size_t i = 0; i < sizeof timer_sources / sizeof timer_sources[0]; i++) {
        const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(timer_sources[i].timer);
        const uint8_t event = timer_sources[i].event;
        if (!cap || event > cap->channels) {
            continue;
        }
        uint32_t prescaler, period;
        if (!divisions(ch32rv_timer_clock_hz(cap->number), hz, per, cap->counter_bits,
                       event != CH32RV_ADC_TRGO, &prescaler, &period)) {
            continue;
        }
        const CH32RVTimerRequest request = {
            cap->number, CH32RV_TIMER_WHOLE, {(uint16_t)(prescaler - 1u), period - 1u, 0u}
        };
        trigger_lease = ch32rv_timer_try_acquire(&request, &trigger_owner);
        if (!ch32rv_timer_lease_valid(&trigger_lease)) {
            continue;
        }
        ch32rv_timer_apply_base(&trigger_lease);
        const uint32_t base = cap->register_base;
        if (event == CH32RV_ADC_TRGO) {
            TIM_CTLR2(base) = TIM_CTLR2_MMS_UP;
        } else {
            TIM_CTLR2(base) = 0;
            const uint32_t shift = (event - 1u) % 2u ? 8u : 0u;
            volatile uint16_t *chctlr = event <= 2u ? &CH32RV_TIM_CHCTLR1(base) : &CH32RV_TIM_CHCTLR2(base);
            *chctlr = (uint16_t)((*chctlr & ~(0xFFu << shift)) | (TIM_OCMODE_PWM1 << shift));
            const uint32_t half = period / 2u;
            if (cap->counter_bits == 32u) {
                CH32RV_TIM_CHCVR32(base, event) = half;
            } else {
                CH32RV_TIM_CHCVR(base, event) = (uint16_t)half;
            }
            CH32RV_TIM_CCER(base) |= (uint16_t)(1u << ((event - 1u) * 4u));
            if (cap->kind == CH32RV_TIMER_KIND_ADVANCED) {
                CH32RV_TIM_BDTR(base) |= CH32RV_TIM_BDTR_MOE;
            }
        }
        t->timer = cap->number;
        t->event = event;
        t->extsel = timer_sources[i].extsel;
        t->prescaler = prescaler;
        t->period = period;
        return true;
    }
    return false;
}

uint32_t ch32rv_adc_timer_trigger_millihz(const ch32rv_adc_timer_trigger_t *t)
{
    if (!t->timer) {
        return 0;
    }
    const uint64_t div = (uint64_t)t->prescaler * t->period;
    return (uint32_t)(((uint64_t)ch32rv_timer_clock_hz(t->timer) * 1000u + div / 2u) / div);
}

void ch32rv_adc_timer_trigger_start(const ch32rv_adc_timer_trigger_t *t)
{
    if (t->timer) {
        ch32rv_timer_start(&trigger_lease);
    }
}

void ch32rv_adc_timer_trigger_stop(const ch32rv_adc_timer_trigger_t *t)
{
    if (t->timer) {
        ch32rv_timer_stop(&trigger_lease);
    }
}

void ch32rv_adc_timer_trigger_end(ch32rv_adc_timer_trigger_t *t)
{
    if (!t->timer) {
        return;
    }
    const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(t->timer);
    if (ch32rv_timer_lease_valid(&trigger_lease)) {
        TIM_CTLR2(cap->register_base) = 0;
        ch32rv_timer_release(&trigger_lease);   /* stops it, its channels off */
    }
    t->timer = 0;
}

/* -------------------------------------------------------------------- pad */

#ifndef CH32RV_EXTICR_FIELD_BITS
#define CH32RV_EXTICR_FIELD_BITS 4
#define CH32RV_EXTICR_FIELDS_PER_REG 4
#endif

/* The remap value the pad needs, -1 when it is no source, 0 where no remap
 * chooses (the EXTI line's own port select does). */
static int pad_remap(uint32_t pad)
{
    if (!ch32rv_pad_is_valid(pad)) {
        return -1;
    }
#ifdef CH32RV_ADC_TRIGGER_PADS
#define PAD(p, remap) if (pad == (uint32_t)(p)) return (remap);
    CH32RV_ADC_TRIGGER_PADS(PAD)
#undef PAD
    return -1;
#else
    return CH32RV_PIN_BIT(pad) == CH32RV_ADC_TRIGGER_LINE ? 0 : -1;
#endif
}

bool ch32rv_adc_pin_trigger_valid(uint32_t pin)
{
    return pad_remap(CH32RV_PIN_RESOLVE(pin)) >= 0;
}

bool ch32rv_adc_pin_trigger_begin(uint32_t pin, uint8_t mode, uint8_t *extsel)
{
    const uint32_t pad = CH32RV_PIN_RESOLVE(pin);
    const int remap = pad_remap(pad);
    if (remap < 0 || !ch32rv_adc_pin_trigger_mode_ok(mode)) {
        return false;
    }
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pad);
    const uint8_t bit = (uint8_t)CH32RV_PIN_BIT(pad);
    ch32rv_clock_enable(AFIO);
    ch32rv_gpio_clock_enable(port);
#ifdef CH32RV_ADC_TRIGGER_REMAP_BIT
    CH32RV_AFIO_PCFR1 = (CH32RV_AFIO_PCFR1 & ~(1u << CH32RV_ADC_TRIGGER_REMAP_BIT)) |
                        ((uint32_t)remap << CH32RV_ADC_TRIGGER_REMAP_BIT);
#endif
    const uint32_t shift = (bit % CH32RV_EXTICR_FIELDS_PER_REG) * CH32RV_EXTICR_FIELD_BITS;
    const uint32_t field_mask = (1u << CH32RV_EXTICR_FIELD_BITS) - 1u;
    volatile uint32_t *cr = &CH32RV_AFIO_EXTICR(bit / CH32RV_EXTICR_FIELDS_PER_REG);
    *cr = (*cr & ~(field_mask << shift)) | ((uint32_t)port << shift);
    const uint32_t mask = 1u << bit;
    if (mode == RISING || mode == CHANGE) {
        CH32RV_EXTI_RTENR |= mask;
    } else {
        CH32RV_EXTI_RTENR &= ~mask;
    }
    if (mode == FALLING || mode == CHANGE) {
        CH32RV_EXTI_FTENR |= mask;
    } else {
        CH32RV_EXTI_FTENR &= ~mask;
    }
    CH32RV_EXTI_EVENR |= mask;
    CH32RV_EXTI_INTFR = mask;
    *extsel = CH32RV_ADC_TRIGGER_EXTSEL;
    return true;
}

void ch32rv_adc_pin_trigger_end(uint32_t pin)
{
    const uint32_t pad = CH32RV_PIN_RESOLVE(pin);
    if (pad_remap(pad) < 0) {
        return;
    }
    const uint32_t mask = 1u << CH32RV_PIN_BIT(pad);
    CH32RV_EXTI_EVENR &= ~mask;
    if ((CH32RV_EXTI_INTENR & mask) == 0u) {     /* no attachInterrupt() on the line */
        CH32RV_EXTI_RTENR &= ~mask;
        CH32RV_EXTI_FTENR &= ~mask;
    }
}
