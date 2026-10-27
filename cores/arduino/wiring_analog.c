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
#ifdef CH32RV_ADC_CAL_CHANNEL
/* Added to every conversion (see ch32rv_adc_begin). */
static int16_t ch32rv_adc_offset;
#endif

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

static uint32_t ch32rv_adc_convert_raw(uint32_t base, uint32_t channel);

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
#ifdef CH32RV_ADC_CAL_CHANNEL
    /* The EVT's Get_CalibrationValue(): VDDA / 2 should read mid-scale; the
     * difference, from the middle six of ten sorted readings, is added to
     * every conversion from now on. */
    {
        uint16_t buf[10];
        for (uint8_t i = 0; i < 10; i++) {
            buf[i] = (uint16_t)ch32rv_adc_convert_raw(base, CH32RV_ADC_CAL_CHANNEL);
        }
        for (uint8_t i = 0; i < 10; i++) {
            for (uint8_t j = 0; j + 1 < 10 - i; j++) {
                if (buf[j] > buf[j + 1]) {
                    const uint16_t t = buf[j];
                    buf[j] = buf[j + 1];
                    buf[j + 1] = t;
                }
            }
        }
        uint32_t sum = 0;
        for (uint8_t i = 2; i < 8; i++) {
            sum += buf[i];
        }
        ch32rv_adc_offset = (int16_t)((int32_t)(1u << (CH32RV_ADC_BITS - 1u)) -
                                      (int32_t)((sum + 3u) / 6u));
    }
#endif
    CH32RV_ADC_MARK_STARTED(index);
}

/* One conversion of `channel` at the hardware's native width, uncorrected. */
static uint32_t ch32rv_adc_convert_raw(uint32_t base, uint32_t channel)
{
    CH32RV_ADC_RSQR1_AT(base) = 0;        /* one conversion in the sequence */
    CH32RV_ADC_RSQR3_AT(base) = channel;
    CH32RV_ADC_CTLR2_AT(base) |= CH32RV_ADC_CTLR2_SWSTART;
    while ((CH32RV_ADC_STATR_AT(base) & CH32RV_ADC_STATR_EOC) == 0u) {
    }
    return CH32RV_ADC_RDATAR_AT(base) & ((1u << CH32RV_ADC_BITS) - 1u);
}

/* The same, with the family's offset correction where it has one. */
static uint32_t ch32rv_adc_convert(uint32_t base, uint32_t channel)
{
    const uint32_t raw = ch32rv_adc_convert_raw(base, channel);
#ifdef CH32RV_ADC_CAL_CHANNEL
    const int32_t value = (int32_t)raw + ch32rv_adc_offset;
    const int32_t full = (int32_t)((1u << CH32RV_ADC_BITS) - 1u);
    return (uint32_t)(value < 0 ? 0 : value > full ? full : value);
#else
    return raw;
#endif
}

#ifdef NUM_ANALOG_INPUTS
/* The pad as an analog input on its instance, started; its channel, or
 * NOT_AN_ANALOG_PIN. */
static uint32_t ch32rv_adc_prepare(pin_size_t pin, uint8_t *index)
{
    pin = CH32RV_PIN_RESOLVE(pin);
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

/* Scale a native-width reading to analogReadResolution()'s width. */
static uint32_t ch32rv_adc_scale(uint32_t value)
{
    if (ch32rv_adc_read_bits > CH32RV_ADC_BITS) {
        value <<= (ch32rv_adc_read_bits - CH32RV_ADC_BITS);
    } else if (ch32rv_adc_read_bits < CH32RV_ADC_BITS) {
        value >>= (CH32RV_ADC_BITS - ch32rv_adc_read_bits);
    }
    return value;
}

#ifdef CH32RV_ADC_VREFINT_CHANNEL
/* One conversion of an internal channel on ADC1, at the native width. The
 * variant names the channels (device-data adc_internal.csv); where they have
 * a switch (CTLR2.TSVREFE, X315's VREFE) it is turned on once and left on -
 * the sensor needs about 10 us to start. The first conversion after a channel
 * change is thrown away: the sample capacitor still holds the last pad. */
static uint32_t ch32rv_adc_read_internal(uint32_t channel)
{
    ch32rv_adc_begin(0);
    const uint32_t base = CH32RV_ADC_BASE(0);
#ifdef CH32RV_ADC_INTERNAL_ENABLE
    if ((CH32RV_ADC_CTLR2_AT(base) & CH32RV_ADC_INTERNAL_ENABLE) == 0u) {
        CH32RV_ADC_CTLR2_AT(base) |= CH32RV_ADC_INTERNAL_ENABLE;
        delayMicroseconds(20);
    }
#endif
#ifdef CH32RV_ADC_BUFFER_ENABLE
    /* The input buffer, on for the internal channel only: the EVT calibrates
     * and reads pads without it. */
    CH32RV_ADC_CTLR1_AT(base) |= CH32RV_ADC_BUFFER_ENABLE;
#endif
    (void)ch32rv_adc_convert(base, channel);
    const uint32_t value = ch32rv_adc_convert(base, channel);
#ifdef CH32RV_ADC_BUFFER_ENABLE
    CH32RV_ADC_CTLR1_AT(base) &= ~CH32RV_ADC_BUFFER_ENABLE;
#endif
    return value;
}
#endif

int analogReadVref(void)
{
#ifdef CH32RV_ADC_VREFINT_CHANNEL
    return (int)ch32rv_adc_scale(ch32rv_adc_read_internal(CH32RV_ADC_VREFINT_CHANNEL));
#else
    return 0;
#endif
}

uint32_t ch32rv_adc_vdd_millivolts(void)
{
#if defined(CH32RV_ADC_VREFINT_CHANNEL) && defined(CH32RV_ADC_VREFINT_MV)
    const uint32_t raw = ch32rv_adc_read_internal(CH32RV_ADC_VREFINT_CHANNEL);
    if (raw == 0u) {
        return 0;
    }
    const uint32_t full = (1u << CH32RV_ADC_BITS) - 1u;
    const uint32_t mv = (CH32RV_ADC_VREFINT_MV * full + raw / 2u) / raw;
    /* No CH32 runs above 5.5 V: a reference that reads lower than that allows
     * is not one, and the answer is "unknown" rather than 245 V. Measured on a
     * CH32X035C8T6, whose ADC_IN15 reads 4-15 (pads convert normally there;
     * an X035F8U6 gave 3.2-3.4 V in October). */
    return mv > 6000u ? 0u : mv;
#else
    return 0;
#endif
}

float analogReadTemp(void)
{
#if defined(CH32RV_ADC_TEMP_CHANNEL) && defined(CH32RV_ADC_VREFINT_MV)
    /* The sensor voltage against Vrefint rather than an assumed 3.3 V supply:
     * T = (V25 - Vsense) / slope + 25, the datasheet's typicals (+/- several
     * degrees uncalibrated). */
    const uint32_t vref = ch32rv_adc_read_internal(CH32RV_ADC_VREFINT_CHANNEL);
    const uint32_t sense = ch32rv_adc_read_internal(CH32RV_ADC_TEMP_CHANNEL);
    if (vref == 0u) {
        return __builtin_nanf("");
    }
    const float mv = (float)sense * (float)CH32RV_ADC_VREFINT_MV / (float)vref;
    return ((float)CH32RV_ADC_TEMP_V25_MV - mv) * 1000.0f / (float)CH32RV_ADC_TEMP_SLOPE_UV + 25.0f;
#else
    return __builtin_nanf("");
#endif
}

int analogRead(pin_size_t pin)
{
#ifdef NUM_ANALOG_INPUTS
    uint8_t index = 0;
    const uint32_t channel = ch32rv_adc_prepare(pin, &index);
    if (channel == NOT_AN_ANALOG_PIN) {
        return 0;
    }
    return (int)ch32rv_adc_scale(ch32rv_adc_convert(CH32RV_ADC_BASE(index), channel));
#else
    (void)pin;
    return 0;
#endif
}
