/* ch32rv_rgb_led_write() (Arduino.h): bytes to WS2812-type LEDs, most
 * significant bit first, on any pad, by the CPU with interrupts off for the
 * whole buffer - one frame with no gap between LEDs. rgbLedWrite() /
 * rgbLedWriteOrdered() send one LED's three bytes through it.
 *
 * Why a buffer and not only arduino-esp32's one-LED call: a chain made of
 * separate calls leaves the line low for the call itself between LEDs, and on
 * V006 at 24 MHz that was 10.6 us from flash and 13 us even with the call in
 * RAM (bench, 2026-11-17) - past older WS2812B's latch of about 9 us, so the
 * chain broke into frames. Only bytes sent in one stretch keep every bit
 * exact. It is the shape of Adafruit_NeoPixel's show(): the bytes in the
 * LEDs' own order, so RGBW parts (SK6812, 4 bytes) work as well.
 *
 * A bit is a high of T0H (0) or T1H (1) and a low to the end of the bit
 * period. The targets sit in the middle of the WS2812 / WS2812B / SK6812
 * windows: T0H 375 ns (0.25-0.55 us), T1H 800 ns (0.65-0.95 us), the bit
 * 1.25 us (0.65-1.85 us). Every edge is a single store to the port's
 * set / clear register, and the store at T0H is the same for both bit values
 * (clear the pad, or clear nothing), so a bit's timing does not depend on its
 * value. Two ways to time the edges, chosen by the clock:
 *
 *  - SysTick at 72 MHz and up (V20x, V30x, H41x and the faster menu
 *    entries): each edge waits for its own tick count from the frame's start
 *    (ch32rv_ticks()), so a slow instruction or a bus wait shifts one edge by
 *    a poll's few cycles and never adds up across the frame.
 *  - Below that the poll would be most of a T0H, and the sequence is counted
 *    in instructions instead: straight-line stores and nops from RAM (no
 *    flash wait states), the counts computed from F_CPU at one cycle an
 *    instruction and two for the loop's taken branch (QingKe's simple
 *    pipeline). At 8 MHz that is T0H 375 ns, T1H 750 ns and a 1.625 us bit
 *    (the loop's own instructions are longer than 1.25 us there). The last
 *    bit of a byte loads the next one in the nops of its low (below 36 MHz
 *    there are not enough of them, and that bit is up to 4 cycles longer:
 *    1.42 us at 24 MHz).
 *
 * The constants below are what the signals test (rgb-led, WireSkein's
 * rgb_led) judges on each series; a core whose stores or branches take
 * longer shows there first. */
#include "Arduino.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_irq.h"
#include "internal/ch32rv_ticks.h"

#define RGB_T0H_NS  375u
#define RGB_T1H_NS  800u
#define RGB_BIT_NS  1250u

#if CH32RV_SYSTICK_HZ >= 72000000u
#define RGB_BY_TICKS 1
#define RGB_TICKS(ns) ((uint32_t)(((CH32RV_SYSTICK_HZ / 1000u) * (ns) + 500000u) / 1000000u))
#else
#define RGB_BY_TICKS 0
/* Cycles, rounded. */
#define RGB_CYCLES(ns) ((((uint32_t)(F_CPU / 1000u)) * (ns) + 500000u) / 1000000u)
#define RGB_POS(x) ((x) > 0 ? (x) : 0)
/* nops after the high store up to the T0H store (the store itself: 1) */
#define RGB_N0 RGB_POS((int)RGB_CYCLES(RGB_T0H_NS) - 1)
/* from the T0H store to the final clear: 2 stores before it */
#define RGB_N1 RGB_POS((int)RGB_CYCLES(RGB_T1H_NS) - 2 - RGB_N0)
/* the rest of the bit: 3 stores, 4 instructions, the taken branch (2) */
#define RGB_N2 RGB_POS((int)RGB_CYCLES(RGB_BIT_NS) - 9 - RGB_N0 - RGB_N1)
#endif

/* The frame, interrupts already off. set / clr: the pad's set and clear
 * registers; p .. end: the bytes (at least one). */
#if RGB_BY_TICKS
CH32RV_RAMFUNC static void rgb_send(volatile uint32_t *set, volatile uint32_t *clr, uint32_t set_mask,
                                    uint32_t clr_mask, const uint8_t *p, const uint8_t *end)
{
    const uint32_t h0 = RGB_TICKS(RGB_T0H_NS), h1 = RGB_TICKS(RGB_T1H_NS), bit = RGB_TICKS(RGB_BIT_NS);
    /* Start shortly after now (T0H's worth): the first edge waits on the same
     * poll as every other one. Every edge has its own tick count from here,
     * so a byte's load never stretches a bit. */
    uint32_t t = ch32rv_ticks() + h0;
    do {
        uint32_t data = (uint32_t)*p++ << 24;
        for (int i = 0; i < 8; i++, data <<= 1) {
            uint32_t at0 = (data & 0x80000000u) ? 0u : clr_mask;
            /* Worked out here, not between the T0H wait and its store (where
             * the compiler put it: three instructions more on T0H only). */
            __asm__ volatile("" : "+r"(at0));
            while ((int32_t)(ch32rv_ticks() - t) < 0) {
            }
            *set = set_mask;
            while ((int32_t)(ch32rv_ticks() - (t + h0)) < 0) {
            }
            *clr = at0;
            while ((int32_t)(ch32rv_ticks() - (t + h1)) < 0) {
            }
            *clr = clr_mask;
            t += bit;
        }
    } while (p != end);
}
#else
/* the last bit's low, which loads the next byte (4 instructions more) */
#define RGB_N2L RGB_POS(RGB_N2 - 4)
CH32RV_RAMFUNC static void rgb_send(volatile uint32_t *set, volatile uint32_t *clr, uint32_t set_mask,
                                    uint32_t clr_mask, const uint8_t *p, const uint8_t *end)
{
    /* Each byte inverted and in bits 31..24, so that one arithmetic shift
     * and one AND give what the T0H store writes: clr_mask for a 0, nothing
     * for a 1. Bits 1-7 of a byte go round loop 1; bit 8 is written out
     * after it, its low loading the next byte. */
    uint32_t d, n, v;
    __asm__ volatile(
        "lbu  %[d], 0(%[p])\n\t"
        "addi %[p], %[p], 1\n\t"
        "not  %[d], %[d]\n\t"
        "slli %[d], %[d], 24\n\t"
        "srai %[v], %[d], 31\n\t"
        "and  %[v], %[v], %[cm]\n"
        "0:\n\t"
        "li   %[n], 7\n"
        "1:\n\t"
        "sw   %[sm], 0(%[set])\n\t"
        ".rept %[n0]\n\tnop\n\t.endr\n\t"
        "sw   %[v], 0(%[clr])\n\t"
        ".rept %[n1]\n\tnop\n\t.endr\n\t"
        "sw   %[cm], 0(%[clr])\n\t"
        "slli %[d], %[d], 1\n\t"
        "srai %[v], %[d], 31\n\t"
        "and  %[v], %[v], %[cm]\n\t"
        "addi %[n], %[n], -1\n\t"
        ".rept %[n2]\n\tnop\n\t.endr\n\t"
        "bnez %[n], 1b\n\t"
        "sw   %[sm], 0(%[set])\n\t"
        ".rept %[n0]\n\tnop\n\t.endr\n\t"
        "sw   %[v], 0(%[clr])\n\t"
        ".rept %[n1]\n\tnop\n\t.endr\n\t"
        "sw   %[cm], 0(%[clr])\n\t"
        "beq  %[p], %[e], 2f\n\t"
        "lbu  %[d], 0(%[p])\n\t"
        "addi %[p], %[p], 1\n\t"
        "not  %[d], %[d]\n\t"
        "slli %[d], %[d], 24\n\t"
        "srai %[v], %[d], 31\n\t"
        "and  %[v], %[v], %[cm]\n\t"
        ".rept %[n2l]\n\tnop\n\t.endr\n\t"
        "j    0b\n"
        "2:\n\t"
        : [d] "=&r"(d), [n] "=&r"(n), [v] "=&r"(v), [p] "+r"(p)
        : [e] "r"(end), [sm] "r"(set_mask), [cm] "r"(clr_mask), [set] "r"(set), [clr] "r"(clr),
          [n0] "i"(RGB_N0), [n1] "i"(RGB_N1), [n2] "i"(RGB_N2), [n2l] "i"(RGB_N2L)
        : "memory");
}
#endif

/* The pad's 4-bit mode nibble. */
static uint32_t rgb_pad_config(uint8_t port, uint8_t bit)
{
    if (bit < 8u) {
        return (CH32RV_GPIO_CFGLR(port) >> (bit * 4u)) & 0xFu;
    }
#if CH32RV_GPIO_PORT_WIDTH > 16
    if (bit >= 16u) {
        return (CH32RV_GPIO_CFGXR(port) >> ((bit - 16u) * 4u)) & 0xFu;
    }
#endif
    return (CH32RV_GPIO_CFGHR(port) >> ((bit - 8u) * 4u)) & 0xFu;
}

#define RGB_CFG_OUT_PP_FAST 0x3u   /* push-pull, the fastest edge (MODE 11) */

/* The first call on a pad: kept in flash, out of the RAM-resident path. */
static __attribute__((noinline)) void rgb_pad_setup(pin_size_t pin, uint8_t port, uint8_t bit)
{
    ch32rv_gpio_clock_enable(port);
    ch32rv_gpio_clear(port, bit);                     /* idle low from the first moment */
    pinMode(pin, OUTPUT);                             /* ends a PWM on the pad */
    ch32rv_gpio_set_config(port, bit, RGB_CFG_OUT_PP_FAST);
}

void ch32rv_rgb_led_write(pin_size_t pin, const uint8_t *data, size_t len)
{
    pin = CH32RV_PIN_RESOLVE(pin);
    if (!ch32rv_pad_is_valid(pin) || len == 0u) {
        return;
    }
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin), bit = (uint8_t)CH32RV_PIN_BIT(pin);
    if (rgb_pad_config(port, bit) != RGB_CFG_OUT_PP_FAST) {
        rgb_pad_setup(pin, port, bit);
    }
#if CH32RV_GPIO_PORT_WIDTH > 16
    volatile uint32_t *set = bit >= 16u ? &CH32RV_GPIO_BSXR(port) : &CH32RV_GPIO_BSHR(port);
    const uint32_t set_mask = 1u << (bit & 15u);
#else
    volatile uint32_t *set = &CH32RV_GPIO_BSHR(port);
    const uint32_t set_mask = 1u << bit;
#endif
    const uint32_t saved = ch32rv_irq_save();
    rgb_send(set, &CH32RV_GPIO_BCR(port), set_mask, 1u << bit, data, data + len);
    ch32rv_irq_restore(saved);
}

void rgbLedWriteOrdered(pin_size_t pin, rgb_led_color_order_t order, uint8_t red_val, uint8_t green_val,
                        uint8_t blue_val)
{
    uint8_t c[3];
    switch (order) {
    case LED_COLOR_ORDER_RGB: c[0] = red_val;   c[1] = green_val; c[2] = blue_val;  break;
    case LED_COLOR_ORDER_BGR: c[0] = blue_val;  c[1] = green_val; c[2] = red_val;   break;
    case LED_COLOR_ORDER_BRG: c[0] = blue_val;  c[1] = red_val;   c[2] = green_val; break;
    case LED_COLOR_ORDER_RBG: c[0] = red_val;   c[1] = blue_val;  c[2] = green_val; break;
    case LED_COLOR_ORDER_GBR: c[0] = green_val; c[1] = blue_val;  c[2] = red_val;   break;
    case LED_COLOR_ORDER_GRB:
    default:                  c[0] = green_val; c[1] = red_val;   c[2] = blue_val;  break;
    }
    ch32rv_rgb_led_write(pin, c, sizeof c);
}

void rgbLedWrite(pin_size_t pin, uint8_t red_val, uint8_t green_val, uint8_t blue_val)
{
    rgbLedWriteOrdered(pin, LED_COLOR_ORDER_GRB, red_val, green_val, blue_val);
}
