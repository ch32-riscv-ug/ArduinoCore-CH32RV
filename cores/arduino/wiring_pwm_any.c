/* analogWrite() on any pad a timer channel reaches: the pad table.
 *
 * The variant lists every pad a timer reaches, on every route device-data
 * gives (CH32RV_TIMER_PADS, ch32rv_timer_pad.c): a channel's own pad, the
 * pads a remap moves it to, and the complementary outputs (CHxN) of the
 * advanced timers. A pad's default PWM comes first; another route is taken
 * when the pad has no default PWM. TIM4/TIM5 are left out where tone() and
 * Servo want them (CH32RV_PWM_TIMER_MASK).
 *
 * One AFIO field moves all of a timer's pads on most families: a pad on
 * another route of a timer that already drives PWM elsewhere takes the timer
 * over, and the timer's pads on the old route stop (low, as analogWrite() of
 * 0). Likewise a channel moving to another pad leaves the old one low.
 *
 * Arduino.h sends a pin known at compile time with a default PWM to
 * ch32rv_pwm_default() (wiring_pwm.c), so this file - the table, the routes,
 * the moving of pads - is linked only where a pin needs it or is chosen at
 * run time.
 */
#include "Arduino.h"
#include "CH32RVTimer.h"
#include "internal/ch32rv_pwm.h"
#include "internal/ch32rv_timer_internal.h"

#ifndef CH32RV_PWM_TIMER_MASK
#define CH32RV_PWM_TIMER_MASK 0u
#endif

/* The route each timer's PWM is on (one AFIO field per timer), and the pad
 * each channel's lease drives. */
static uint8_t pwm_route[CH32RV_TIMER_COUNT];
static uint8_t pwm_pin[CH32RV_TIMER_COUNT][4];

/* The PWM pads that `pad` would move off its timer: its channel's other
 * pad, and (where one AFIO field moves all of a timer's pads) the timer's
 * PWM on another route. With `stop`, they stop - low, as analogWrite() of 0,
 * before the route changes under them. True when there are any. */
static __attribute__((noinline)) bool pwm_displaces(const CH32RVTimerPad *pad, int index,
                                                    bool stop)
{
    bool any = false;
    for (unsigned c = 0; c < 4u; c++) {
        const uint8_t other = pwm_pin[index][c];
        if (ch32rv_pwm_lease[index][c].timer != 0u && other != pad->pin &&
            (c == pad->channel - 1u
#if !CH32RV_GPIO_PER_PIN_AF
             || pwm_route[index] != pad->route
#endif
             )) {
            any = true;
            if (stop) {
                ch32rv_pwm_level(other, 0);
            }
        }
    }
    return any;
}

/* The way this pad has PWM, in the table's order (its default PWM first):
 * the first channel or complementary output that fits beside the PWM already
 * running, else the first one (which then takes its timer channel or route
 * over). -1 when the pad reaches none, else the timer's index. */
static int pwm_pad(pin_size_t pin, CH32RVTimerPad *out)
{
    const uint8_t roles = CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH) |
                          CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CHN);
    int first = -1;
    uint8_t first_n = 0;
    CH32RVTimerPad pad;
    for (uint8_t n = 0; ch32rv_timer_pad_find(pin, roles, n, &pad); n++) {
        const int i = ch32rv_timer_index(pad.timer);
        if (i < 0 || ((CH32RV_PWM_TIMER_MASK >> pad.timer) & 1u) == 0u) {
            continue;
        }
        if (!pwm_displaces(&pad, i, false)) {
            *out = pad;
            return i;
        }
        if (first < 0) {
            first = i;
            first_n = n;
        }
    }
    if (first >= 0) {
        ch32rv_timer_pad_find(pin, roles, first_n, out);
    }
    return first;
}

/* Replaces wiring_pwm.c's weak one: the pads `pad` moves off its timer stop
 * (low), and the timer's route and the channel's pad are recorded. */
void ch32rv_pwm_make_room(const CH32RVTimerPad *pad, int index)
{
    pwm_displaces(pad, index, true);
    pwm_route[index] = pad->route;
    pwm_pin[index][pad->channel - 1u] = pad->pin;
}

/* Replaces wiring_pwm.c's weak one: a pad's PWM may be on any route. */
CH32RVTimerLease *ch32rv_pwm_slot_of(pin_size_t pin)
{
    for (size_t i = 0; i < CH32RV_TIMER_COUNT * 4u; i++) {
        CH32RVTimerLease *lease = &ch32rv_pwm_lease[0][0] + i;
        if (lease->timer != 0u && (&pwm_pin[0][0])[i] == pin) {
            return lease;
        }
    }
    return 0;
}

/* analogWrite() on any pad: the pad table (ch32rv_timer_pad.c) picks the
 * way. Arduino.h sends a pin known at compile time with a default PWM to
 * ch32rv_pwm_default() instead, which links no table; the name in brackets
 * is the function itself, not that macro. */
void (analogWrite)(pin_size_t pin, int value)
{
    pin = CH32RV_PIN_RESOLVE(pin);
    if (ch32rv_pwm_dac(pin, value)) {
        return;
    }
    const uint32_t full = (1u << ch32rv_pwm_write_bits) - 1u;
    CH32RVTimerPad pad;
    const int index = value <= 0 || (uint32_t)value >= full ? -1 : pwm_pad(pin, &pad);
    if (index < 0) {
        /* No PWM here, or 0 / full scale: a plain level, as the AVR core does.
         * A PWM running on the pad stops and its timer channel is given back. */
        ch32rv_pwm_level(pin, value > 0);
        return;
    }
    ch32rv_pwm_start(pin, value, &pad, index);
}
