/* analogWrite() on a pad with a default PWM, and what every analogWrite()
 * shares; the pad table's analogWrite() is wiring_pwm_any.c.
 *
 * The variant lists every pad a timer reaches, on every route device-data
 * gives (CH32RV_TIMER_PADS, ch32rv_timer_pad.c): a channel's own pad, the
 * pads a remap moves it to, and the complementary outputs (CHxN) of the
 * advanced timers. A pad's default PWM (the variant's CH32RV_PWM_PIN_TO_*,
 * generated from device-data) comes first; another route is taken when the
 * pad has no default PWM. TIM4/TIM5 are left out where tone() and Servo want
 * them (CH32RV_PWM_TIMER_MASK). Pads with no PWM fall back to a plain digital
 * level, which is what the AVR core does for non-PWM pins.
 *
 * One AFIO field moves all of a timer's pads on most families: a pad on
 * another route of a timer that already drives PWM elsewhere takes the timer
 * over, and the timer's pads on the old route stop (low, as analogWrite() of
 * 0). Likewise a channel moving to another pad leaves the old one low.
 */
#include "Arduino.h"
#include "CH32RVTimer.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_timer_internal.h"
#include "internal/ch32rv_pwm.h"

#include <stdbool.h>

/* The fewest counter steps a period may have: analogWrite()'s default 0..255
 * stays exact, and analogWriteFrequency() is capped at F_CPU / 256. */
#define CH32RV_PWM_STEPS 256u
#define CH32RV_PWM_HZ    1000u

#ifndef CH32RV_PWM_PIN_COUNT
/* A series with no default PWM pad. */
#define CH32RV_PWM_PIN_TO_TIMER(p)   ((void)(p), 0)
#define CH32RV_PWM_PIN_TO_CHANNEL(p) ((void)(p), 0)
#endif
#ifndef CH32RV_PWM_TIMER_MASK
#define CH32RV_PWM_TIMER_MASK 0u
#endif

uint8_t ch32rv_pwm_write_bits = 8;
static uint32_t ch32rv_pwm_hz = CH32RV_PWM_HZ;

static const uint8_t ch32rv_pwm_owner_identity;
static const CH32RVTimerOwner ch32rv_pwm_owner = {
    &ch32rv_pwm_owner_identity, 0, 0
};

/* Each pad's PWM holds its timer channel through a lease kept here, with the
 * pad and route it drives, so the pad gives the channel back when its PWM
 * stops: analogWrite() of 0 or full scale (a plain level, as the AVR core
 * does), pinMode() or digitalWrite() on the pad. The timer stops when none of
 * its channels is held (ch32rv_timer_release), and tone(), Servo or a capture
 * can take it again. Until 2026-11-05 a lease was never given back: one
 * analogWrite() kept the timer from them until a reset. A slot's timer 0 is
 * an empty slot. */
CH32RVTimerLease ch32rv_pwm_lease[CH32RV_TIMER_COUNT][4];
static uint8_t ch32rv_pwm_held;

/* The slot of the pad's PWM: its default timer channel. wiring_pwm_any.c,
 * where pads have PWM on other routes too, replaces this with a search of
 * the pads it records. */
__attribute__((weak)) CH32RVTimerLease *ch32rv_pwm_slot_of(pin_size_t pin)
{
    const uint8_t channel = (uint8_t)CH32RV_PWM_PIN_TO_CHANNEL(pin);
    const int index = ch32rv_timer_index((uint8_t)CH32RV_PWM_PIN_TO_TIMER(pin));
    return index >= 0 && channel != 0u ? &ch32rv_pwm_lease[index][channel - 1u] : 0;
}

bool ch32rv_pwm_stop(pin_size_t pin)
{
    if (ch32rv_pwm_held == 0u) {
        return false;      /* the common case, cheap for digitalWrite() */
    }
    CH32RVTimerLease *lease = ch32rv_pwm_slot_of(pin);
    if (!lease || lease->timer == 0u) {
        return false;
    }
    /* A lease another owner took over is only forgotten here. */
    ch32rv_timer_release(lease);
    ch32rv_pwm_held--;
    return true;
}

/* A plain level, as analogWrite() of 0 or full scale (and the pad whose PWM
 * another pad takes, wiring_pwm_any.c). */
void ch32rv_pwm_level(pin_size_t pin, int high)
{
    ch32rv_pwm_stop(pin);
    digitalWrite(pin, high ? HIGH : LOW);
    pinMode(pin, OUTPUT);
}

/* The timer of a pad with no default PWM, from the pad table: weak here,
 * ch32rv_timer_pad.c's where analogWrite() on such a pad links it. Without
 * the table no such pad has PWM, and any timer's clock will do. */
__attribute__((weak)) uint8_t ch32rv_timer_pad_first_timer(uint32_t pin)
{
    (void)pin;
    return 0;
}

void analogWriteFrequency(pin_size_t pin, uint32_t frequency)
{
    pin = CH32RV_PIN_RESOLVE(pin);
    uint8_t timer = (uint8_t)CH32RV_PWM_PIN_TO_TIMER(pin);
    if (timer == 0u) {
        timer = ch32rv_timer_pad_first_timer(pin);
    }
    const uint32_t top = ch32rv_timer_clock_hz(timer) / CH32RV_PWM_STEPS;
    if (frequency == 0u) {
        return;
    }
    ch32rv_pwm_hz = frequency > top ? top : frequency;
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

/* The DAC's pads: true when the value went to the converter. */
bool ch32rv_pwm_dac(pin_size_t pin, int value)
{
#if defined(CH32RV_DAC1_PIN) || defined(CH32RV_DAC2_PIN)
    /* Scale the caller's range onto the converter's 12 bits before the PWM
     * path gets a chance to treat the pad as an ordinary pin. */
    const uint32_t full = (1u << ch32rv_pwm_write_bits) - 1u;
    uint32_t level = (value <= 0) ? 0u : (uint32_t)value;
    if (level > full) {
        level = full;
    }
    level = (level * 0x0FFFu) / full;
    return dac_write(pin, level);
#else
    (void)pin;
    (void)value;
    return false;
#endif
}

/* The timer channel's route on the families with one AFIO field per timer.
 * Only ch32rv_timer_pad.c writes the field, and it replaces this weak one:
 * where it is not linked nothing has moved a timer off its reset route (0),
 * which is where analogWrite()'s default pads are (ch32rv_pwm_default). */
__attribute__((weak)) void ch32rv_timer_route_set(uint8_t timer, uint8_t route)
{
    (void)timer;
    (void)route;
}

/* Room for `pad` on its timer: wiring_pwm.c's own, linked with the pad
 * table, stops the pads it moves off the timer. Here, where only the default
 * pads have PWM, there are none: each default pad has a channel of its own
 * on the timer's reset route. */
__attribute__((weak)) void ch32rv_pwm_make_room(const CH32RVTimerPad *pad, int index)
{
    (void)pad;
    (void)index;
}

/* PWM of `value` (0 < value < full scale) on `pad`, the timer at `index`. */
void ch32rv_pwm_start(pin_size_t pin, int value, const CH32RVTimerPad *padp, int index)
{
    const CH32RVTimerPad pad = *padp;
    const uint32_t full = (1u << ch32rv_pwm_write_bits) - 1u;
    const CH32RVTimerCapability *cap = ch32rv_timer_capability((size_t)index);
    const uint8_t channel = pad.channel;

    /* The period in timer ticks, split into the smallest prescaler that fits
     * the 16-bit counter and a counter length rounded to the frequency. A
     * fixed 256-step counter could only divide F_CPU by whole multiples of
     * 256: asked for 100 kHz, CH32H417 (100 MHz) gave 130 kHz and CH32X035
     * (48 MHz) would give 187.5 kHz. */
    const uint32_t period = ch32rv_timer_clock_hz(cap->number) / ch32rv_pwm_hz;
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

    ch32rv_pwm_make_room(&pad, index);

    const CH32RVTimerRequest request = {
        pad.timer,
        channel,
        {(uint16_t)(prescale - 1u), (uint16_t)(steps - 1u),
         CH32RV_TIM_CTLR1_ARPE}
    };
    CH32RVTimerLease lease = ch32rv_timer_takeover(&request, &ch32rv_pwm_owner);
    if (!ch32rv_timer_lease_valid(&lease) || !ch32rv_timer_apply_base(&lease)) {
        return;
    }
    if (ch32rv_pwm_lease[index][channel - 1u].timer == 0u) {
        ch32rv_pwm_held++;
    }
    ch32rv_pwm_lease[index][channel - 1u] = lease;
    /* The pad to the channel: its alternate function (per-pin families) or
     * the timer's route, then an alternate-function output. */
    ch32rv_gpio_clock_enable((uint8_t)CH32RV_PIN_PORT(pin));
#if CH32RV_GPIO_PER_PIN_AF
    ch32rv_pin_set_af((uint8_t)pin, pad.route);
#else
    ch32rv_timer_route_set(pad.timer, pad.route);
#endif
    ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(pin), (uint8_t)CH32RV_PIN_BIT(pin),
                           CH32RV_GPIO_CFG_AF_PP_50M);
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
    /* CCxE drives the channel's pad, CCxNE its complementary one: alone,
     * CHxN follows the reference as CHx would (not inverted). */
    const uint32_t ccer_shift = (channel - 1u) * 4u;
    uint16_t ccer = CH32RV_TIM_CCER(base) & (uint16_t)~(0xFu << ccer_shift);
    ccer |= (uint16_t)((pad.role == CH32RV_TIMER_PAD_CHN ? 4u : 1u) << ccer_shift);
    CH32RV_TIM_CCER(base) = ccer;
    if (cap->kind == CH32RV_TIMER_KIND_ADVANCED) {
        /* Advanced timers keep every channel output gated behind MOE. */
        CH32RV_TIM_BDTR(base) |= CH32RV_TIM_BDTR_MOE;
    }
    ch32rv_timer_start(&lease);
}

/* analogWrite() on a pad with a default PWM (the variant's
 * CH32RV_PWM_PIN_TO_*: the timer's reset route, or the pad's alternate
 * function on the per-pin families), with no pad table. */
void ch32rv_pwm_default(pin_size_t pin, int value)
{
    pin = CH32RV_PIN_RESOLVE(pin);
    if (ch32rv_pwm_dac(pin, value)) {
        return;
    }
    const uint32_t full = (1u << ch32rv_pwm_write_bits) - 1u;
    const CH32RVTimerPad pad = {
        (uint8_t)pin, (uint8_t)CH32RV_PWM_PIN_TO_TIMER(pin), (uint8_t)CH32RV_PWM_PIN_TO_CHANNEL(pin),
        CH32RV_TIMER_PAD_CH,
#ifdef CH32RV_PWM_PIN_TO_AF
        (uint8_t)CH32RV_PWM_PIN_TO_AF(pin)
#else
        0
#endif
    };
    const int index = ch32rv_timer_index(pad.timer);
    if (value <= 0 || (uint32_t)value >= full || index < 0 || pad.channel == 0u) {
        ch32rv_pwm_level(pin, value > 0);
        return;
    }
    ch32rv_pwm_start(pin, value, &pad, index);
}
