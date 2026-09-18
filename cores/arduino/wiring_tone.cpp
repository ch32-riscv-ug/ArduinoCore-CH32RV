/* tone() / noTone().
 *
 * A timer interrupt toggles the pin, which is what AVR does and what makes the
 * call work on any pin rather than only on the handful a timer's compare
 * output can reach. The variant supplies a preferred timer with an update
 * vector (CH32_TONE_TIMER, see generate.py). The resource manager first tries
 * it without disruption, then another free timer, and only then takes over the
 * preferred one. If that timer carries PWM, its analogWrite() channels stop -
 * the same limitation the AVR core documents for pins 3 and 11.
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
#include "CH32Timer.h"
#include "ch32_gpio.h"

#ifdef CH32_TONE_TIMER

/* The pin currently sounding, and how many toggles are left. Read by the ISR,
 * written by tone()/noTone() with the timer stopped, so no lock is needed. */
static volatile uint8_t tone_pin = 0xFF;
static volatile uint8_t tone_port;
static volatile uint8_t tone_bit;
static volatile uint32_t tone_toggles;     /* 0 = until noTone() */
static CH32TimerLease tone_lease;
static const uint8_t tone_owner_identity = 0;

static void tone_quiesce(void *, uint8_t, uint8_t)
{
    if (tone_pin != 0xFF) {
        ch32_gpio_clear(tone_port, tone_bit);
    }
    tone_pin = 0xFF;
    tone_toggles = 0;
    tone_lease = {};
}

static const CH32TimerOwner tone_owner = {
    &tone_owner_identity, tone_quiesce, nullptr
};

static void tone_stop(void)
{
    if (ch32TimerLeaseValid(&tone_lease)) {
        ch32TimerDetachUpdateInterrupt(&tone_lease);
        ch32TimerRelease(&tone_lease);
    }
    if (tone_pin != 0xFF) {
        ch32_gpio_clear(tone_port, tone_bit);
    }
    tone_pin = 0xFF;
    tone_toggles = 0;
}

static void tone_update(void *)
{
    if (ch32_gpio_read(tone_port, tone_bit)) {
        ch32_gpio_clear(tone_port, tone_bit);
    } else {
        ch32_gpio_set(tone_port, tone_bit);
    }

    if (tone_toggles != 0u && --tone_toggles == 0u) {
        tone_stop();
    }
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

    CH32TimerRequest request = {
        CH32_TONE_TIMER, CH32_TIMER_WHOLE,
        {(uint16_t)psc, ticks - 1u, 0}
    };
    tone_lease = ch32TimerTryAcquire(&request, &tone_owner);
    if (!ch32TimerLeaseValid(&tone_lease)) {
        request.timer = CH32_TIMER_ANY;
        tone_lease = ch32TimerTryAcquire(&request, &tone_owner);
    }
    if (!ch32TimerLeaseValid(&tone_lease)) {
        request.timer = CH32_TONE_TIMER;
        tone_lease = ch32TimerTakeover(&request, &tone_owner);
    }
    if (!ch32TimerApplyBase(&tone_lease) ||
        !ch32TimerAttachUpdateInterrupt(&tone_lease, tone_update, nullptr) ||
        !ch32TimerStart(&tone_lease)) {
        tone_stop();
    }
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
