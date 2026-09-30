/* analogRead().
 *
 * The variant's generated pin map already says which pad is which channel, so
 * this file only has to drive the peripheral.
 *
 * Almost everywhere that means ADC1 and nothing else: a part with several ADCs
 * normally repeats the same channel numbers on the same pads, so the extra
 * instances reach nothing new and A<n> stays unambiguous. CH32X305 and
 * CH32X315 are the exception - their four ADCs sit on disjoint pads, and 36 of
 * X315's 48 analog pads have no A<n> at all. There the variant defines
 * CH32RV_ADC_INSTANCE_COUNT > 1 and this file follows CH32RV_PIN_TO_ADC_INSTANCE.
 *
 * The instance is a compile-time constant 0 on every other series, so that
 * code folds away and those binaries do not pay for this.
 */
#include "Arduino.h"
#ifndef CH32RV_ADC_MAX_HZ
#error "CH32RV_ADC_MAX_HZ is required (see build.core_defines in boards.txt)"
#endif
#include "ch32rv_gpio.h"
#include "ch32rv_registers.h"

#ifndef CH32RV_ADC_BITS
#error "CH32RV_ADC_BITS is required (see build.core_defines in boards.txt)"
#endif

/* Arduino sketches assume 0..1023 unless told otherwise. */
static uint8_t ch32rv_adc_read_bits = 10;
static uint8_t ch32rv_adc_started;

#if defined(CH32RV_ADC_INSTANCE_COUNT) && CH32RV_ADC_INSTANCE_COUNT > 1
/* base, and the RCC register and bit that turns the instance on. */
static const struct {
    uint32_t base;
    uint32_t clken_addr;
    uint32_t clken_mask;
} ch32rv_adc_instances[CH32RV_ADC_INSTANCE_COUNT] = CH32RV_ADC_INSTANCES;

#define CH32RV_ADC_INDEX(pin)  ((uint8_t)(CH32RV_PIN_TO_ADC_INSTANCE(pin) - 1u))
#define CH32RV_ADC_BASE(i)     (ch32rv_adc_instances[i].base)
#define CH32RV_ADC_IS_STARTED(i)   ((ch32rv_adc_started >> (i)) & 1u)
#define CH32RV_ADC_MARK_STARTED(i) (ch32rv_adc_started |= (uint8_t)(1u << (i)))
#define CH32RV_ADC_ENABLE_CLOCK(i) \
    (CH32RV_REG32(ch32rv_adc_instances[i].clken_addr) |= \
     ch32rv_adc_instances[i].clken_mask)
#else
/* One instance: every one of these folds to the ADC1 constant. */
#define CH32RV_ADC_INDEX(pin)      ((void)(pin), 0u)
#define CH32RV_ADC_BASE(i)         ((void)(i), CH32RV_ADC1_BASE)
#define CH32RV_ADC_IS_STARTED(i)   ((void)(i), ch32rv_adc_started)
#define CH32RV_ADC_MARK_STARTED(i) ((void)(i), ch32rv_adc_started = 1)
#define CH32RV_ADC_ENABLE_CLOCK(i) ((void)(i), ch32rv_clock_enable(ADC1))
#endif

void analogReadResolution(int bits)
{
    if (bits > 0 && bits <= 32) {
        ch32rv_adc_read_bits = (uint8_t)bits;
    }
}

void analogReference(uint8_t mode)
{
    /* CH32 has no selectable reference: the ADC always measures against the
     * analog supply. Accepting the call keeps portable sketches compiling. */
    (void)mode;
}

static void ch32rv_adc_begin(uint8_t index)
{
    if (CH32RV_ADC_IS_STARTED(index)) {
        return;
    }
    const uint32_t base = CH32RV_ADC_BASE(index);
    CH32RV_ADC_ENABLE_CLOCK(index);

    /* ADCCLK has to stay inside the family's own ceiling, which is not the
     * same number everywhere: 14 MHz on CH32V103/V20x/V30x, 48 on CH32L103,
     * 64 on CH32V205, 80 on CH32X315, and as low as 6 on CH32X035 and CH32V003
     * at their lower supply voltages. boards.txt carries it, read out of
     * operating_conditions.csv. Pick the smallest divider that stays under. */
#if defined(CH32RV_ADC_CLK_CTLR3) && CH32RV_ADC_CLK_CTLR3
    /* CH32X035: the divider is in the ADC itself, /4 to /16 in steps of one,
     * and RCC_CFGR0 has no ADCPRE bits to write (ch32rv_registers.h). */
    uint32_t divider = 4;
    while (divider < 16 && (F_CPU / divider) > CH32RV_ADC_MAX_HZ) {
        divider += 1;
    }
    CH32RV_ADC_CTLR3_AT(base) = (CH32RV_ADC_CTLR3_AT(base) & ~CH32RV_ADC_CTLR3_CLK_DIV_MASK) |
                              CH32RV_ADC_CTLR3_CLK_DIV(divider);
#else
    uint32_t divider = 2;
    while (divider < 8 && (F_CPU / divider) > CH32RV_ADC_MAX_HZ) {
        divider += 2;
    }
    CH32RV_RCC_CFGR0 = (CH32RV_RCC_CFGR0 & ~CH32RV_RCC_CFGR0_ADCPRE_MASK) |
                     CH32RV_RCC_CFGR0_ADCPRE((divider / 2u) - 1u);
#endif

    /* Longest sample time on every channel. analogRead() is not a fast path,
     * and high source impedance is the norm on a breadboard. */
    CH32RV_ADC_SAMPTR1_AT(base) = 0x00FFFFFFu;
    CH32RV_ADC_SAMPTR2_AT(base) = 0x3FFFFFFFu;
    CH32RV_ADC_CTLR1_AT(base) = 0;
    /* Software trigger: EXTSEL = SWSTART, with the external trigger enabled. */
    CH32RV_ADC_CTLR2_AT(base) = CH32RV_ADC_CTLR2_EXTSEL_SWSTART |
                              CH32RV_ADC_CTLR2_EXTTRIG;

    CH32RV_ADC_CTLR2_AT(base) |= CH32RV_ADC_CTLR2_ADON;
    for (volatile int settle = 0; settle < 1000; settle++) {
    }
    CH32RV_ADC_CTLR2_AT(base) |= CH32RV_ADC_CTLR2_RSTCAL;
    while (CH32RV_ADC_CTLR2_AT(base) & CH32RV_ADC_CTLR2_RSTCAL) {
    }
    CH32RV_ADC_CTLR2_AT(base) |= CH32RV_ADC_CTLR2_CAL;
    while (CH32RV_ADC_CTLR2_AT(base) & CH32RV_ADC_CTLR2_CAL) {
    }
    CH32RV_ADC_MARK_STARTED(index);
}

/* One conversion of `channel` at the hardware's native width. */
static uint32_t ch32rv_adc_convert(uint32_t base, uint32_t channel)
{
    CH32RV_ADC_RSQR1_AT(base) = 0;        /* one conversion in the sequence */
    CH32RV_ADC_RSQR3_AT(base) = channel;
    CH32RV_ADC_CTLR2_AT(base) |= CH32RV_ADC_CTLR2_SWSTART;
    while ((CH32RV_ADC_STATR_AT(base) & CH32RV_ADC_STATR_EOC) == 0u) {
    }
    return CH32RV_ADC_RDATAR_AT(base) & ((1u << CH32RV_ADC_BITS) - 1u);
}

#ifdef NUM_ANALOG_INPUTS
/* The pad as an analog input on its instance, started; its channel, or
 * NOT_AN_ANALOG_PIN. */
static uint32_t ch32rv_adc_prepare(pin_size_t pin, uint8_t *index)
{
    const uint32_t channel = CH32RV_PIN_TO_ADC_CHANNEL(pin);
    if (channel == NOT_AN_ANALOG_PIN) {
        return channel;
    }
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin);
    ch32rv_gpio_clock_enable(port);
    ch32rv_gpio_set_config(port, (uint8_t)CH32RV_PIN_BIT(pin),
                         CH32RV_GPIO_CFG_IN_ANALOG);
    *index = CH32RV_ADC_INDEX(pin);
    ch32rv_adc_begin(*index);
    return channel;
}
#endif

int analogRead(pin_size_t pin)
{
#ifdef NUM_ANALOG_INPUTS
    uint8_t index = 0;
    const uint32_t channel = ch32rv_adc_prepare(pin, &index);
    if (channel == NOT_AN_ANALOG_PIN) {
        return 0;
    }
    uint32_t value = ch32rv_adc_convert(CH32RV_ADC_BASE(index), channel);

    /* Scale the hardware's native width to the requested one, both ways. */
    if (ch32rv_adc_read_bits > CH32RV_ADC_BITS) {
        value <<= (ch32rv_adc_read_bits - CH32RV_ADC_BITS);
    } else if (ch32rv_adc_read_bits < CH32RV_ADC_BITS) {
        value >>= (CH32RV_ADC_BITS - ch32rv_adc_read_bits);
    }
    return (int)value;
#else
    (void)pin;
    return 0;
#endif
}
