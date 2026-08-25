/* tone() / noTone().
 *
 * A timer interrupt toggles the pin, which is what AVR does and what makes the
 * call work on any pin rather than only on the handful a timer's compare
 * output can reach. Which timer is the variant's business: it picks one with
 * an interrupt vector of its own, preferring one no PWM pad uses
 * (CH32_TONE_TIMER, see generate.py). Where no such timer is free the variant
 * says so with CH32_TONE_SHARES_PWM, and analogWrite() on that timer's pads is
 * disturbed while a tone plays - the same limitation the AVR core documents
 * for pins 3 and 11.
 *
 * The cost is one interrupt per half period: 2 kHz costs 4000 interrupts a
 * second. That is the price of "any pin", and it is what the Arduino API
 * promises.
 *
 * C++ rather than C only because of linkage: api/Common.h declares tone() and
 * noTone() among the C++ prototypes (they take a default argument), so a C
 * definition would export an unmangled symbol that no sketch ever references.
 */
#include "Arduino.h"
#include "ch32_gpio.h"
#include "ch32_registers.h"

#ifdef CH32_TONE_TIMER

/* Variants generated before the 32-bit timer was known do not say. 16 is what
 * every family except CH32L103/V205/V20x/X3x5 has. */
#ifndef CH32_TONE_TIMER_BITS
#define CH32_TONE_TIMER_BITS 16
#endif

/* The pin currently sounding, and how many toggles are left. Read by the ISR,
 * written by tone()/noTone() with the timer stopped, so no lock is needed. */
static volatile uint8_t tone_pin = 0xFF;
static volatile uint8_t tone_port;
static volatile uint8_t tone_bit;
static volatile uint32_t tone_toggles;     /* 0 = until noTone() */

static void tone_stop(void)
{
    CH32_TIM_CTLR1(CH32_TONE_TIMER_BASE) = 0;
    CH32_TIM_DMAINTENR(CH32_TONE_TIMER_BASE) = 0;
    ch32_irq_disable(CH32_TONE_TIMER_IRQ);
    tone_pin = 0xFF;
}

void tone(uint8_t pin, unsigned int frequency, unsigned long duration)
{
    if (!digitalPinIsValid(pin)) {
        /* Nothing to do: a pin that does not exist cannot be the one playing,
         * and stopping the tone that *is* playing would break the rule below
         * that a sounding tone on another pin wins. */
        return;
    }
    if (frequency == 0u) {
        noTone(pin);
        return;
    }
    /* AVR's rule: a tone already sounding on another pin wins. Without it a
     * library that forgets to call noTone() silently steals the speaker. */
    if (tone_pin != 0xFF && tone_pin != pin) {
        return;
    }

    /* Interrupt at twice the frequency - one toggle is half a period. The
     * prescaler only comes in when the count will not fit 16 bits, which for
     * the lowest audible tones it does not: 31 Hz at 48 MHz is 774193 ticks. */
    uint32_t psc = 0;
    uint32_t ticks = (uint32_t)(F_CPU / (2UL * (uint32_t)frequency));
    while (ticks > 0x10000UL) {
        psc++;
        ticks = (uint32_t)(F_CPU / ((psc + 1UL) * 2UL * (uint32_t)frequency));
    }
    if (ticks == 0u) {
        ticks = 1u;                 /* frequency above what the timer can do */
    }
    if (psc > 0xFFFFu) {
        return;                     /* below what the timer can do at all */
    }

    /* Number of toggles the duration asks for, computed before the timer
     * starts so the ISR only ever counts down. */
    uint32_t toggles = 0;
    if (duration > 0ul) {
        /* duration_ms * 2 * f / 1000, split so it stays in 32 bits without
         * pulling the 64-bit division helper into a 16 KB part. */
        const uint32_t f = (uint32_t)frequency;
        toggles = (uint32_t)(duration / 500ul) * f +
                  ((uint32_t)(duration % 500ul) * f) / 500u;
        if (toggles == 0u) {
            toggles = 1u;           /* a duration shorter than one half period */
        }
    }

    tone_stop();

    const uint8_t port = (uint8_t)CH32_PIN_PORT(pin);
    const uint8_t bit = (uint8_t)CH32_PIN_BIT(pin);
    ch32_gpio_clock_enable(port);
    ch32_gpio_set_config(port, bit, CH32_GPIO_CFG_OUT_PP_10M);
    ch32_gpio_clear(port, bit);

    tone_port = port;
    tone_bit = bit;
    tone_toggles = toggles;
    tone_pin = pin;

    ch32_clock_enable_at(CH32_TONE_TIMER_CLKEN_ADDR, CH32_TONE_TIMER_CLKEN_MASK);
    CH32_TIM_PSC(CH32_TONE_TIMER_BASE) = (uint16_t)psc;
#if CH32_TONE_TIMER_BITS == 32
    /* A 16-bit store here would land in both halves of the 32-bit register and
     * ask for a reload 65537 times later than intended, which is what made
     * tone() silent on CH32L103. */
    CH32_TIM_ATRLR32(CH32_TONE_TIMER_BASE) = ticks - 1u;
#else
    CH32_TIM_ATRLR(CH32_TONE_TIMER_BASE) = (uint16_t)(ticks - 1u);
#endif
    /* Load PSC and ATRLR now rather than at the first overflow, then drop the
     * update flag that loading them raised - otherwise the first interrupt
     * arrives immediately and the first half period is short. */
    CH32_TIM_SWEVGR(CH32_TONE_TIMER_BASE) = CH32_TIM_SWEVGR_UG;
    CH32_TIM_INTFR(CH32_TONE_TIMER_BASE) = 0;
    CH32_TIM_DMAINTENR(CH32_TONE_TIMER_BASE) = CH32_TIM_INT_UIE;
    ch32_irq_enable(CH32_TONE_TIMER_IRQ);
    CH32_TIM_CTLR1(CH32_TONE_TIMER_BASE) = CH32_TIM_CTLR1_CEN;
}

void noTone(uint8_t pin)
{
    /* Only the pin that is playing. Treating an unknown pin as "stop whatever
     * is running" made tone(bogus_pin) silence a tone on a real one. */
    if (tone_pin != 0xFF && pin == tone_pin) {
        tone_stop();
    }
    if (digitalPinIsValid(pin)) {
        /* Left low, not floating: a speaker held at half rail draws current
         * and hums. */
        ch32_gpio_clear((uint8_t)CH32_PIN_PORT(pin), (uint8_t)CH32_PIN_BIT(pin));
    }
}

/* The vector table names the handler, so this one keeps C linkage. */
extern "C" __attribute__((interrupt)) void CH32_TONE_TIMER_HANDLER(void)
{
    CH32_TIM_INTFR(CH32_TONE_TIMER_BASE) = (uint16_t)~CH32_TIM_INT_UIE;

    if (ch32_gpio_read(tone_port, tone_bit)) {
        ch32_gpio_clear(tone_port, tone_bit);
    } else {
        ch32_gpio_set(tone_port, tone_bit);
    }

    if (tone_toggles != 0u && --tone_toggles == 0u) {
        tone_stop();
        ch32_gpio_clear(tone_port, tone_bit);
    }
}

#else  /* the variant found no timer with an interrupt of its own */

void tone(uint8_t pin, unsigned int frequency, unsigned long duration)
{
    (void)pin;
    (void)frequency;
    (void)duration;
}

void noTone(uint8_t pin)
{
    (void)pin;
}

#endif
