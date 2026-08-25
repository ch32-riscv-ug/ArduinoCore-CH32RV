/* attachInterrupt() on EXTI.
 *
 * EXTI has one line per pin *bit*: line 3 is PA3 or PB3 or PC3, never two at
 * once, and AFIO_EXTICR picks which port drives it. digitalPinToInterrupt() is
 * therefore the identity - the pin number already carries port and bit.
 *
 * The handlers are shared: the vector table groups the lines
 * (EXTI7_0 / EXTI15_8 on the small parts, EXTI0..4 plus EXTI9_5 and EXTI15_10
 * elsewhere), so every generated handler funnels into ch32_exti_dispatch().
 */
#include "Arduino.h"
#include "ch32_gpio.h"
#include "ch32_registers.h"

#define CH32_EXTI_LINES 16

static voidFuncPtrParam ch32_exti_callback[CH32_EXTI_LINES];
static void *ch32_exti_param[CH32_EXTI_LINES];

/* A plain voidFuncPtr is stored in the same table by wrapping it: the wrapper
 * receives the function pointer itself as its parameter. */
static void ch32_exti_call_plain(void *fn)
{
    ((voidFuncPtr)fn)();
}

static void ch32_exti_set(pin_size_t pin, PinStatus mode,
                          voidFuncPtrParam callback, void *param)
{
    const uint8_t port = (uint8_t)CH32_PIN_PORT(pin);
    const uint8_t bit = (uint8_t)CH32_PIN_BIT(pin);

    if (port >= CH32_PORT_COUNT || bit >= CH32_EXTI_LINES) {
        return;   /* bits 16..23 (X033/X035) have no EXTI line */
    }

    ch32_exti_callback[bit] = callback;
    ch32_exti_param[bit] = param;

    ch32_clock_enable(AFIO);
    ch32_gpio_clock_enable(port);

    const uint32_t shift = (bit & 3u) * 4u;
    volatile uint32_t *cr = &CH32_AFIO_EXTICR(bit >> 2);
    *cr = (*cr & ~(0xFu << shift)) | ((uint32_t)port << shift);

    const uint32_t mask = 1u << bit;
    if (mode == RISING || mode == CHANGE) {
        CH32_EXTI_RTENR |= mask;
    } else {
        CH32_EXTI_RTENR &= ~mask;
    }
    if (mode == FALLING || mode == CHANGE || mode == LOW) {
        CH32_EXTI_FTENR |= mask;
    } else {
        CH32_EXTI_FTENR &= ~mask;
    }
    CH32_EXTI_INTFR = mask;          /* drop anything pending from setup */
    CH32_EXTI_INTENR |= mask;

    /* Enable whichever vector covers this line. */
#define CH32_EXTI_ENABLE(handler, group_mask, irqn) \
    if (group_mask & mask) ch32_irq_enable(irqn);
    CH32_EXTI_GROUPS(CH32_EXTI_ENABLE)
#undef CH32_EXTI_ENABLE
}

void attachInterruptParam(pin_size_t pin, voidFuncPtrParam callback,
                          PinStatus mode, void *param)
{
    if (callback) {
        ch32_exti_set(pin, mode, callback, param);
    }
}

void attachInterrupt(pin_size_t pin, voidFuncPtr callback, PinStatus mode)
{
    if (callback) {
        ch32_exti_set(pin, mode, ch32_exti_call_plain, (void *)callback);
    }
}

void detachInterrupt(pin_size_t pin)
{
    const uint8_t bit = (uint8_t)CH32_PIN_BIT(pin);

    if (CH32_PIN_PORT(pin) >= CH32_PORT_COUNT || bit >= CH32_EXTI_LINES) {
        return;
    }
    const uint32_t mask = 1u << bit;
    CH32_EXTI_INTENR &= ~mask;
    CH32_EXTI_RTENR &= ~mask;
    CH32_EXTI_FTENR &= ~mask;
    CH32_EXTI_INTFR = mask;
    ch32_exti_callback[bit] = 0;
    ch32_exti_param[bit] = 0;
}

/* Clearing INTFR before the callback means an edge that arrives while the
 * callback runs is not lost. */
static void ch32_exti_dispatch(uint32_t lines)
{
    uint32_t pending = CH32_EXTI_INTFR & lines;

    while (pending) {
        const uint32_t bit = (uint32_t)__builtin_ctz(pending);
        pending &= ~(1u << bit);
        CH32_EXTI_INTFR = 1u << bit;
        if (ch32_exti_callback[bit]) {
            ch32_exti_callback[bit](ch32_exti_param[bit]);
        }
    }
}

/* One ISR per EXTI vector this variant has; the names come from the generated
 * vector table, so a family that groups the lines differently needs no change
 * here. */
#define CH32_EXTI_DEFINE_ISR(handler, group_mask, irqn)      \
    __attribute__((interrupt)) void handler(void)            \
    {                                                        \
        ch32_exti_dispatch(group_mask);                      \
    }
CH32_EXTI_GROUPS(CH32_EXTI_DEFINE_ISR)
#undef CH32_EXTI_DEFINE_ISR
