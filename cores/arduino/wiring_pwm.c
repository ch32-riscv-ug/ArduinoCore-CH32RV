/* analogWrite() on TIM1/TIM2/TIM3.
 *
 * The variant says which timer and channel a pad reaches on its default route
 * (generated from device-data). Pads with no PWM fall back to a plain digital
 * level, which is what the AVR core does for non-PWM pins.
 */
#include "Arduino.h"
#include "CH32Timer.h"
#include "ch32_gpio.h"
#include "ch32_registers.h"

#include <stdbool.h>

/* Arduino's analogWrite() takes 0..255 unless analogWriteResolution() says
 * otherwise. Counting to exactly 256 keeps duty = value / 256. */
#define CH32_PWM_STEPS 256u
#define CH32_PWM_HZ    1000u

#ifndef CH32_PWM_PIN_COUNT
/* X305/X315 reach their timers only through per-pin alternate-function
 * selectors, which the variant cannot describe yet (docs/todo.ja.md). Every
 * pin then falls through to the digital path below. */
#define CH32_PWM_PIN_TO_TIMER(p)   ((void)(p), 0)
#define CH32_PWM_PIN_TO_CHANNEL(p) ((void)(p), 0)
#endif

static uint8_t ch32_pwm_write_bits = 8;
static const uint8_t ch32_pwm_owner_identity;
static const CH32TimerOwner ch32_pwm_owner = {
    &ch32_pwm_owner_identity, 0, 0
};

void analogWriteResolution(int bits)
{
    if (bits > 0 && bits <= 16) {
        ch32_pwm_write_bits = (uint8_t)bits;
    }
}

#if defined(CH32_DAC1_PIN) || defined(CH32_DAC2_PIN)
/* Drive the converter instead of a timer. The pad goes to analog mode - as an
 * input, which is what the reference manual asks for: the DAC drives the pin
 * from inside, and leaving the digital output driver on would fight it. */
static bool dac_write(pin_size_t pin, uint32_t value12)
{
    uint32_t enable;
    volatile uint32_t *holding;

#if defined(CH32_DAC1_PIN)
    if (pin == CH32_DAC1_PIN) {
        enable = CH32_DAC_CTLR_EN1;
        holding = &CH32_DAC_R12BDHR1;
    } else
#endif
#if defined(CH32_DAC2_PIN)
    if (pin == CH32_DAC2_PIN) {
        enable = CH32_DAC_CTLR_EN2;
        holding = &CH32_DAC_R12BDHR2;
    } else
#endif
    {
        return false;
    }

    const uint8_t port = (uint8_t)CH32_PIN_PORT(pin);
    ch32_gpio_clock_enable(port);
    ch32_gpio_set_config(port, (uint8_t)CH32_PIN_BIT(pin),
                         CH32_GPIO_CFG_IN_ANALOG);
    ch32_clock_enable(DAC);
    CH32_DAC_CTLR |= enable;
    *holding = value12 & 0x0FFFu;
    return true;
}
#endif

void analogWrite(pin_size_t pin, int value)
{
#if defined(CH32_DAC1_PIN) || defined(CH32_DAC2_PIN)
    {
        /* Scale the caller's range onto the converter's 12 bits before the
         * PWM path gets a chance to treat the pad as an ordinary pin. */
        const uint32_t full = (1u << ch32_pwm_write_bits) - 1u;
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
    const uint8_t timer = (uint8_t)CH32_PWM_PIN_TO_TIMER(pin);
    const uint8_t channel = (uint8_t)CH32_PWM_PIN_TO_CHANNEL(pin);
    const CH32TimerCapability *cap = ch32TimerCapabilityFor(timer);

    if (!cap || channel == 0u) {
        /* No PWM here: behave like the AVR core and just pick a level. */
        pinMode(pin, OUTPUT);
        digitalWrite(pin, value ? HIGH : LOW);
        return;
    }

    /* Scale the caller's range onto the timer's 0..CH32_PWM_STEPS. */
    const uint32_t full = (1u << ch32_pwm_write_bits) - 1u;
    uint32_t duty = (value <= 0) ? 0u : (uint32_t)value;
    if (duty >= full) {
        duty = CH32_PWM_STEPS;
    } else {
        duty = (duty * CH32_PWM_STEPS) / (full + 1u);
    }

    const uint8_t port = (uint8_t)CH32_PIN_PORT(pin);
    ch32_gpio_clock_enable(port);
    ch32_gpio_set_config(port, (uint8_t)CH32_PIN_BIT(pin),
                         CH32_GPIO_CFG_AF_PP_50M);

    uint32_t prescale = F_CPU / (CH32_PWM_HZ * CH32_PWM_STEPS);
    if (prescale == 0u) {
        prescale = 1u;
    }
    const CH32TimerRequest request = {
        timer,
        channel,
        {(uint16_t)(prescale - 1u), CH32_PWM_STEPS - 1u,
         CH32_TIM_CTLR1_ARPE}
    };
    CH32TimerLease lease = ch32TimerTakeover(&request, &ch32_pwm_owner);
    if (!ch32TimerLeaseValid(&lease) || !ch32TimerApplyBase(&lease)) {
        return;
    }
    const uint32_t base = cap->register_base;

    /* Channels 1 and 2 share CHCTLR1, 3 and 4 share CHCTLR2; even channels sit
     * in the high byte of their word. */
    const uint32_t shift = ((channel - 1u) & 1u) * 8u;
    volatile uint16_t *chctlr = (channel <= 2) ? &CH32_TIM_CHCTLR1(base)
                                               : &CH32_TIM_CHCTLR2(base);
    *chctlr = (uint16_t)((*chctlr & ~(0xFFu << shift)) |
                         (CH32_TIM_OCMODE_PWM1 << shift));
    CH32_TIM_CHCVR(base, channel) = (uint16_t)duty;
    CH32_TIM_CCER(base) |= (uint16_t)(1u << ((channel - 1u) * 4u));
    if (cap->kind == CH32_TIMER_KIND_ADVANCED) {
        /* Advanced timers keep every channel output gated behind MOE. */
        CH32_TIM_BDTR(base) |= CH32_TIM_BDTR_MOE;
    }
    ch32TimerStart(&lease);
}
