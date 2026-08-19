/* analogWrite() on TIM1/TIM2/TIM3.
 *
 * The variant says which timer and channel a pad reaches on its default route
 * (generated from device-data). Pads with no PWM fall back to a plain digital
 * level, which is what the AVR core does for non-PWM pins.
 */
#include "Arduino.h"
#include "ch32_gpio.h"
#include "ch32_registers.h"

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
static uint8_t ch32_pwm_started;    /* bit per timer index */

void analogWriteResolution(int bits)
{
    if (bits > 0 && bits <= 16) {
        ch32_pwm_write_bits = (uint8_t)bits;
    }
}

static uint32_t timer_base(uint8_t timer)
{
    switch (timer) {
    case 1:  return CH32_TIM1_BASE;
    case 2:  return CH32_TIM2_BASE;
    case 3:  return CH32_TIM3_BASE;
    default: return 0;
    }
}

static void timer_begin(uint8_t timer, uint32_t base)
{
    if (ch32_pwm_started & (1u << timer)) {
        return;
    }
    if (timer == 1) {
        CH32_RCC_APB2PCENR |= CH32_RCC_APB2_TIM1;
    } else {
        CH32_RCC_APB1PCENR |= (timer == 2) ? CH32_RCC_APB1_TIM2
                                           : CH32_RCC_APB1_TIM3;
    }
    /* One PWM period every CH32_PWM_STEPS counts, at roughly CH32_PWM_HZ. */
    uint32_t prescale = F_CPU / (CH32_PWM_HZ * CH32_PWM_STEPS);
    if (prescale == 0) {
        prescale = 1;
    }
    CH32_TIM_PSC(base) = (uint16_t)(prescale - 1u);
    CH32_TIM_ATRLR(base) = (uint16_t)(CH32_PWM_STEPS - 1u);
    CH32_TIM_CTLR1(base) = CH32_TIM_CTLR1_ARPE | CH32_TIM_CTLR1_CEN;
    if (timer == 1) {
        /* The advanced timer keeps its outputs disabled until MOE is set. */
        CH32_TIM_BDTR(base) |= CH32_TIM_BDTR_MOE;
    }
    ch32_pwm_started |= (uint8_t)(1u << timer);
}

void analogWrite(pin_size_t pin, int value)
{
    const uint8_t timer = (uint8_t)CH32_PWM_PIN_TO_TIMER(pin);
    const uint8_t channel = (uint8_t)CH32_PWM_PIN_TO_CHANNEL(pin);
    const uint32_t base = timer_base(timer);

    if (base == 0) {
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
    timer_begin(timer, base);

    /* Channels 1 and 2 share CHCTLR1, 3 and 4 share CHCTLR2; even channels sit
     * in the high byte of their word. */
    const uint32_t shift = ((channel - 1u) & 1u) * 8u;
    volatile uint16_t *chctlr = (channel <= 2) ? &CH32_TIM_CHCTLR1(base)
                                               : &CH32_TIM_CHCTLR2(base);
    *chctlr = (uint16_t)((*chctlr & ~(0xFFu << shift)) |
                         (CH32_TIM_OCMODE_PWM1 << shift));
    CH32_TIM_CHCVR(base, channel) = (uint16_t)duty;
    CH32_TIM_CCER(base) |= (uint16_t)(1u << ((channel - 1u) * 4u));
}
