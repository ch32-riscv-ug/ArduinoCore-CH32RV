/* analogWrite() on TIM1..TIM5.
 *
 * The variant says which timer and channel a pad reaches on its default route
 * (generated from device-data) - on the per-pin families (V205, X305, X315)
 * also the alternate-function number that connects the pad to it. TIM4/TIM5
 * pads are listed only where tone() and Servo do not want that timer. Pads
 * with no PWM fall back to a plain digital level, which is what the AVR core
 * does for non-PWM pins.
 */
#include "Arduino.h"
#include "CH32RVTimer.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

#include <stdbool.h>

/* The fewest counter steps a period may have: analogWrite()'s default 0..255
 * stays exact, and analogWriteFrequency() is capped at F_CPU / 256. */
#define CH32RV_PWM_STEPS 256u
#define CH32RV_PWM_HZ    1000u

#ifndef CH32RV_PWM_PIN_COUNT
/* A series with no timer channel on any pad: every pin falls through to the
 * digital path below. */
#define CH32RV_PWM_PIN_TO_TIMER(p)   ((void)(p), 0)
#define CH32RV_PWM_PIN_TO_CHANNEL(p) ((void)(p), 0)
#endif

static uint8_t ch32rv_pwm_write_bits = 8;
static uint32_t ch32rv_pwm_hz = CH32RV_PWM_HZ;

void analogWriteFrequency(pin_size_t pin, uint32_t frequency)
{
    (void)pin;
    const uint32_t top = CH32RV_HCLK_HZ / CH32RV_PWM_STEPS;
    if (frequency == 0u) {
        return;
    }
    ch32rv_pwm_hz = frequency > top ? top : frequency;
}
static const uint8_t ch32rv_pwm_owner_identity;
static const CH32RVTimerOwner ch32rv_pwm_owner = {
    &ch32rv_pwm_owner_identity, 0, 0
};

/* Each pad's PWM holds its timer channel through a lease kept here, so the pad
 * gives the channel back when its PWM stops: analogWrite() of 0 or full scale
 * (a plain level, as the AVR core does), pinMode() or digitalWrite() on the
 * pad. The timer stops when none of its channels is held
 * (ch32rv_timer_release), and tone(), Servo or a capture can take it again.
 * Until 2026-11-05 a lease was never given back: one analogWrite() kept the
 * timer from them until a reset. A slot's timer 0 is an empty slot. */
static CH32RVTimerLease ch32rv_pwm_lease[CH32RV_TIMER_COUNT][4];
static uint8_t ch32rv_pwm_held;

static CH32RVTimerLease *ch32rv_pwm_slot(uint8_t timer, uint8_t channel)
{
    if (channel == 0u || channel > 4u) {
        return 0;
    }
    for (size_t i = 0; i < CH32RV_TIMER_COUNT; i++) {
        const CH32RVTimerCapability *cap = ch32rv_timer_capability(i);
        if (cap && cap->number == timer) {
            return &ch32rv_pwm_lease[i][channel - 1u];
        }
    }
    return 0;
}

bool ch32rv_pwm_stop(pin_size_t pin)
{
    if (ch32rv_pwm_held == 0u) {
        return false;      /* the common case, cheap for digitalWrite() */
    }
    CH32RVTimerLease *slot = ch32rv_pwm_slot((uint8_t)CH32RV_PWM_PIN_TO_TIMER(pin),
                                             (uint8_t)CH32RV_PWM_PIN_TO_CHANNEL(pin));
    if (!slot || slot->timer == 0u) {
        return false;
    }
    /* A lease another owner took over is only forgotten here. */
    ch32rv_timer_release(slot);
    ch32rv_pwm_held--;
    return true;
}

void analogWriteResolution(int bits)
{
    if (bits > 0 && bits <= 16) {
        ch32rv_pwm_write_bits = (uint8_t)bits;
    }
}

#if defined(CH32RV_DAC1_PIN) || defined(CH32RV_DAC2_PIN)
/* Drive the converter instead of a timer. The pad goes to analog mode - as an
 * input, which is what the reference manual asks for: the DAC drives the pin
 * from inside, and leaving the digital output driver on would fight it. */
static bool dac_write(pin_size_t pin, uint32_t value12)
{
    uint32_t enable;
    volatile uint32_t *holding;

#if defined(CH32RV_DAC1_PIN)
    if (pin == CH32RV_DAC1_PIN) {
        enable = CH32RV_DAC_CTLR_EN1;
        holding = &CH32RV_DAC_R12BDHR1;
    } else
#endif
#if defined(CH32RV_DAC2_PIN)
    if (pin == CH32RV_DAC2_PIN) {
        enable = CH32RV_DAC_CTLR_EN2;
        holding = &CH32RV_DAC_R12BDHR2;
    } else
#endif
    {
        return false;
    }

    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin);
    ch32rv_gpio_clock_enable(port);
    ch32rv_gpio_set_config(port, (uint8_t)CH32RV_PIN_BIT(pin),
                         CH32RV_GPIO_CFG_IN_ANALOG);
    ch32rv_clock_enable(DAC);
    CH32RV_DAC_CTLR |= enable;
    *holding = value12 & 0x0FFFu;
    return true;
}
#endif

void analogWrite(pin_size_t pin, int value)
{
    pin = CH32RV_PIN_RESOLVE(pin);
#if defined(CH32RV_DAC1_PIN) || defined(CH32RV_DAC2_PIN)
    {
        /* Scale the caller's range onto the converter's 12 bits before the
         * PWM path gets a chance to treat the pad as an ordinary pin. */
        const uint32_t full = (1u << ch32rv_pwm_write_bits) - 1u;
        uint32_t level = (value <= 0) ? 0u : (uint32_t)value;
        if (level > full) {
            level = full;
        }
        level = (level * 0x0FFFu) / full;
        if (dac_write(pin, level)) {
            return;
        }
    }
#endif
    const uint8_t timer = (uint8_t)CH32RV_PWM_PIN_TO_TIMER(pin);
    const uint8_t channel = (uint8_t)CH32RV_PWM_PIN_TO_CHANNEL(pin);
    const CH32RVTimerCapability *cap = ch32rv_timer_capability_for(timer);

    const uint32_t full = (1u << ch32rv_pwm_write_bits) - 1u;
    if (!cap || channel == 0u || value <= 0 || (uint32_t)value >= full) {
        /* No PWM here, or 0 / full scale: a plain level, as the AVR core does.
         * A PWM running on the pad stops and its timer channel is given back. */
        ch32rv_pwm_stop(pin);
        digitalWrite(pin, value > 0 ? HIGH : LOW);
        pinMode(pin, OUTPUT);
        return;
    }

    /* The period in timer ticks, split into the smallest prescaler that fits
     * the 16-bit counter and a counter length rounded to the frequency. A
     * fixed 256-step counter could only divide F_CPU by whole multiples of
     * 256: asked for 100 kHz, CH32H417 (100 MHz) gave 130 kHz and CH32X035
     * (48 MHz) would give 187.5 kHz. */
    const uint32_t period = CH32RV_HCLK_HZ / ch32rv_pwm_hz;
    uint32_t prescale = (period + 65535u) / 65536u;
    if (prescale == 0u) {
        prescale = 1u;
    } else if (prescale > 65536u) {
        prescale = 65536u;             /* the 16-bit prescaler's end: ~1 Hz at 48 MHz */
    }
    uint32_t steps = (period + prescale / 2u) / prescale;
    if (steps > 65536u) {
        steps = 65536u;
    } else if (steps < CH32RV_PWM_STEPS) {
        steps = CH32RV_PWM_STEPS;
    }

    /* Scale the caller's range onto the counter's 0..steps (0 < value < full
     * here). value < full <= 65535 and steps <= 65536: the product stays
     * under 2^32, so no 64-bit multiply (114 B of __muldi3 on V003). */
    const uint32_t duty = ((uint32_t)value * steps) / (full + 1u);

    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin);
    ch32rv_gpio_clock_enable(port);
    ch32rv_gpio_set_config(port, (uint8_t)CH32RV_PIN_BIT(pin),
                         CH32RV_GPIO_CFG_AF_PP_50M);
#ifdef CH32RV_PWM_PIN_TO_AF
    ch32rv_pin_set_af((uint8_t)pin, (uint32_t)CH32RV_PWM_PIN_TO_AF(pin));
#endif

    const CH32RVTimerRequest request = {
        timer,
        channel,
        {(uint16_t)(prescale - 1u), (uint16_t)(steps - 1u),
         CH32RV_TIM_CTLR1_ARPE}
    };
    CH32RVTimerLease lease = ch32rv_timer_takeover(&request, &ch32rv_pwm_owner);
    if (!ch32rv_timer_lease_valid(&lease) || !ch32rv_timer_apply_base(&lease)) {
        return;
    }
    CH32RVTimerLease *slot = ch32rv_pwm_slot(timer, channel);
    if (slot) {
        if (slot->timer == 0u) {
            ch32rv_pwm_held++;
        }
        *slot = lease;
    }
    const uint32_t base = cap->register_base;

    /* Channels 1 and 2 share CHCTLR1, 3 and 4 share CHCTLR2; even channels sit
     * in the high byte of their word. */
    const uint32_t shift = ((channel - 1u) & 1u) * 8u;
    volatile uint16_t *chctlr = (channel <= 2) ? &CH32RV_TIM_CHCTLR1(base)
                                               : &CH32RV_TIM_CHCTLR2(base);
    *chctlr = (uint16_t)((*chctlr & ~(0xFFu << shift)) |
                         (CH32RV_TIM_OCMODE_PWM1 << shift));
    /* 100 % is a compare value past the counter's end: steps itself, which is
     * 65536 only when the counter has all 65536 steps - then one short. */
    /* A 32-bit timer's compare register takes a 16-bit store in both halves
     * (ch32rv_registers.h): the compare would land far past the period and
     * the pad stay high - H417's TIM9 / TIM10 did, until 2026-11-12. */
    if (cap->counter_bits == 32u) {
        CH32RV_TIM_CHCVR32(base, channel) = duty > 65535u ? 65535u : duty;
    } else {
        CH32RV_TIM_CHCVR(base, channel) = (uint16_t)(duty > 65535u ? 65535u : duty);
    }
    CH32RV_TIM_CCER(base) |= (uint16_t)(1u << ((channel - 1u) * 4u));
    if (cap->kind == CH32RV_TIMER_KIND_ADVANCED) {
        /* Advanced timers keep every channel output gated behind MOE. */
        CH32RV_TIM_BDTR(base) |= CH32RV_TIM_BDTR_MOE;
    }
    ch32rv_timer_start(&lease);
}
