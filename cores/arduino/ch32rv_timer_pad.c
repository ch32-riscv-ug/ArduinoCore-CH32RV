/* Where a timer's channels reach: the variant's CH32RV_TIMER_PADS (every
 * route device-data gives) and the AFIO field that selects a timer's route.
 * A file of its own: only analogWrite(), pulseIn() and the timer libraries
 * link the table. */
#include "Arduino.h"
#include "CH32RVTimer.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

#ifdef CH32RV_TIMER_PAD_COUNT

typedef struct {
    uint8_t pin;
    uint8_t timer;
    uint8_t channel_role;    /* channel | role << 4 */
    uint8_t route;
} TimerPadRow;

static const TimerPadRow pad_rows[CH32RV_TIMER_PAD_COUNT] = CH32RV_TIMER_PADS;

__attribute__((noinline)) bool ch32rv_timer_pad_find(uint32_t pin, uint8_t roles, uint8_t n,
                           CH32RVTimerPad *out)
{
    pin = CH32RV_PIN_RESOLVE(pin);
    for (const TimerPadRow *row = pad_rows; row < pad_rows + CH32RV_TIMER_PAD_COUNT; row++) {
        if (row->pin != pin || ((roles >> (row->channel_role >> 4)) & 1u) == 0u) {
            continue;
        }
        if (n-- == 0u) {
            out->pin = row->pin;
            out->timer = row->timer;
            out->channel = row->channel_role & 0x0Fu;
            out->role = row->channel_role >> 4;
            out->route = row->route;
            return true;
        }
    }
    return false;
}

#else

__attribute__((noinline)) bool ch32rv_timer_pad_find(uint32_t pin, uint8_t roles, uint8_t n,
                           CH32RVTimerPad *out)
{
    (void)pin;
    (void)roles;
    (void)n;
    (void)out;
    return false;
}

#endif

#if !CH32RV_GPIO_PER_PIN_AF && defined(CH32RV_TIMER_REMAP_COUNT)

typedef struct {
    uint8_t timer;
    uint8_t bit[4];          /* PCFR1 bit n, 0x80 | n for PCFR2; 0xFF unused */
} TimerRemap;

static const TimerRemap remaps[CH32RV_TIMER_REMAP_COUNT] = CH32RV_TIMER_REMAPS;

static const TimerRemap *remap_of(uint8_t timer)
{
    for (size_t i = 0; i < CH32RV_TIMER_REMAP_COUNT; i++) {
        if (remaps[i].timer == timer) {
            return &remaps[i];
        }
    }
    return 0;
}

uint8_t ch32rv_timer_route(uint8_t timer)
{
    const TimerRemap *r = remap_of(timer);
    if (!r) {
        return 0;
    }
    ch32rv_clock_enable(AFIO);
    const uint32_t pcfr1 = CH32RV_AFIO_PCFR1, pcfr2 = CH32RV_AFIO_PCFR2;
    uint8_t route = 0;
    for (unsigned i = 0; i < 4u && r->bit[i] != 0xFFu; i++) {
        const uint32_t word = (r->bit[i] & 0x80u) ? pcfr2 : pcfr1;
        route |= (uint8_t)(((word >> (r->bit[i] & 0x1Fu)) & 1u) << i);
    }
    return route;
}

/* Bit by bit: a field that spans PCFR1 and PCFR2 (L103) has no single
 * write anyway, and the steps between are a few cycles of AFIO. */
void ch32rv_timer_route_set(uint8_t timer, uint8_t route)
{
    const TimerRemap *r = remap_of(timer);
    if (!r) {
        return;                  /* one route only: nothing to select */
    }
    ch32rv_clock_enable(AFIO);
    for (unsigned i = 0; i < 4u && r->bit[i] != 0xFFu; i++) {
        volatile uint32_t *reg = (r->bit[i] & 0x80u) ? &CH32RV_AFIO_PCFR2 : &CH32RV_AFIO_PCFR1;
        const uint32_t bit = 1u << (r->bit[i] & 0x1Fu);
        if ((route >> i) & 1u) {
            *reg |= bit;
        } else {
            *reg &= ~bit;
        }
    }
}

#else

uint8_t ch32rv_timer_route(uint8_t timer)
{
    (void)timer;
    return 0;
}

void ch32rv_timer_route_set(uint8_t timer, uint8_t route)
{
    (void)timer;
    (void)route;
}

#endif

#if CH32RV_GPIO_PER_PIN_AF && defined(CH32RV_TIMER_PAD_COUNT)
/* The function number a pad has now (ch32rv_pin_set_af's register). */
static uint8_t pin_af(uint8_t pin)
{
    const uint32_t port = (uint32_t)CH32RV_PIN_PORT(pin);
    const uint32_t bit = (uint32_t)CH32RV_PIN_BIT(pin);
    if (bit >= 16u || port >= CH32RV_AFIO_AF_PORTS) {
        return 0xFFu;
    }
    const uint32_t reg = CH32RV_REG32(CH32RV_AFIO_BASE + CH32RV_AFIO_AFLR_OFFSET + port * 8u + (bit >= 8u ? 4u : 0u));
    return (uint8_t)((reg >> ((bit & 7u) * 4u)) & 0xFu);
}

/* An input reaches the timer from every pad set to its function, and a
 * pad's function is 0 from reset: V205's TIM1 break (AF0 on PA6, PB9, PB12,
 * PE15) saw the other pads too, and broke while the chosen one was high
 * (2026-11-17 bench). The timer's other pads for the same input that still
 * have that function are moved off it (to function 15, which they do not
 * use for it). */
static void keep_input_alone(const CH32RVTimerPad *pad)
{
    for (const TimerPadRow *row = pad_rows; row < pad_rows + CH32RV_TIMER_PAD_COUNT; row++) {
        if (row->pin != pad->pin && row->timer == pad->timer && (row->channel_role >> 4) == pad->role &&
            (row->channel_role & 0x0Fu) == pad->channel && pin_af(row->pin) == row->route) {
            ch32rv_pin_set_af(row->pin, 0xFu);
        }
    }
}
#endif

__attribute__((noinline)) void ch32rv_timer_pad_connect(const CH32RVTimerPad *pad, bool output)
{
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pad->pin);
    const uint8_t bit = (uint8_t)CH32RV_PIN_BIT(pad->pin);
    ch32rv_gpio_clock_enable(port);
#if CH32RV_GPIO_PER_PIN_AF
    ch32rv_pin_set_af(pad->pin, pad->route);
#ifdef CH32RV_TIMER_PAD_COUNT
    if (!output) {
        keep_input_alone(pad);
    }
#endif
#else
    ch32rv_timer_route_set(pad->timer, pad->route);
#endif
    if (output) {
        ch32rv_gpio_set_config(port, bit, CH32RV_GPIO_CFG_AF_PP_50M);
    }
}

bool ch32rv_timer_pads_together(const CH32RVTimerPad *a, const CH32RVTimerPad *b)
{
#if CH32RV_GPIO_PER_PIN_AF
    return a->timer == b->timer;
#else
    return a->timer == b->timer && a->route == b->route;
#endif
}

/* The timer analogWriteFrequency() counts on for a pad without a default
 * PWM (wiring_pwm.c has a weak one for sketches without this table). */
uint8_t ch32rv_timer_pad_first_timer(uint32_t pin)
{
    CH32RVTimerPad pad;
    return ch32rv_timer_pad_find(pin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH) | CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CHN),
                                 0, &pad) ? pad.timer : 0u;
}
