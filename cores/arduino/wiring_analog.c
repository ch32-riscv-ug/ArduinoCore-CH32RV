/* analogRead() on ADC1.
 *
 * The variant's generated pin map already says which pad is which ADC1
 * channel, so this file only has to drive the peripheral. Only ADC1 is wired
 * up: parts with several ADCs repeat the same channel numbers on the others,
 * which would make A<n> ambiguous (see choose_uarts' sibling logic in
 * tools/generate/generate.py and docs/todo.ja.md).
 */
#include "Arduino.h"
#ifndef CH32_ADC_MAX_HZ
#error "CH32_ADC_MAX_HZ is required (see build.core_defines in boards.txt)"
#endif
#include "ch32_gpio.h"
#include "ch32_registers.h"

#ifndef CH32_ADC_BITS
#error "CH32_ADC_BITS is required (see build.core_defines in boards.txt)"
#endif

/* Arduino sketches assume 0..1023 unless told otherwise. */
static uint8_t ch32_adc_read_bits = 10;
static uint8_t ch32_adc_started;

void analogReadResolution(int bits)
{
    if (bits > 0 && bits <= 32) {
        ch32_adc_read_bits = (uint8_t)bits;
    }
}

void analogReference(uint8_t mode)
{
    /* CH32 has no selectable reference: the ADC always measures against the
     * analog supply. Accepting the call keeps portable sketches compiling. */
    (void)mode;
}

static void ch32_adc_begin(void)
{
    if (ch32_adc_started) {
        return;
    }
    ch32_clock_enable(ADC1);

    /* ADCCLK has to stay inside the family's own ceiling, which is not the
     * same number everywhere: 14 MHz on CH32V103/V20x/V30x, 48 on CH32L103,
     * 64 on CH32V205, 80 on CH32X315, and as low as 6 on CH32X035 and CH32V003
     * at their lower supply voltages. boards.txt carries it, read out of
     * operating_conditions.csv. Pick the smallest divider that stays under. */
    uint32_t divider = 2;
    while (divider < 8 && (F_CPU / divider) > CH32_ADC_MAX_HZ) {
        divider += 2;
    }
    CH32_RCC_CFGR0 = (CH32_RCC_CFGR0 & ~CH32_RCC_CFGR0_ADCPRE_MASK) |
                     CH32_RCC_CFGR0_ADCPRE((divider / 2u) - 1u);

    /* Longest sample time on every channel. analogRead() is not a fast path,
     * and high source impedance is the norm on a breadboard. */
    CH32_ADC_SAMPTR1 = 0x00FFFFFFu;
    CH32_ADC_SAMPTR2 = 0x3FFFFFFFu;
    CH32_ADC_CTLR1 = 0;
    /* Software trigger: EXTSEL = SWSTART, with the external trigger enabled. */
    CH32_ADC_CTLR2 = CH32_ADC_CTLR2_EXTSEL_SWSTART | CH32_ADC_CTLR2_EXTTRIG;

    CH32_ADC_CTLR2 |= CH32_ADC_CTLR2_ADON;
    for (volatile int settle = 0; settle < 1000; settle++) {
    }
    CH32_ADC_CTLR2 |= CH32_ADC_CTLR2_RSTCAL;
    while (CH32_ADC_CTLR2 & CH32_ADC_CTLR2_RSTCAL) {
    }
    CH32_ADC_CTLR2 |= CH32_ADC_CTLR2_CAL;
    while (CH32_ADC_CTLR2 & CH32_ADC_CTLR2_CAL) {
    }
    ch32_adc_started = 1;
}

int analogRead(pin_size_t pin)
{
#ifdef NUM_ANALOG_INPUTS
    const uint32_t channel = CH32_PIN_TO_ADC_CHANNEL(pin);
    if (channel == NOT_AN_ANALOG_PIN) {
        return 0;
    }
    const uint8_t port = (uint8_t)CH32_PIN_PORT(pin);
    ch32_gpio_clock_enable(port);
    ch32_gpio_set_config(port, (uint8_t)CH32_PIN_BIT(pin),
                         CH32_GPIO_CFG_IN_ANALOG);

    ch32_adc_begin();
    CH32_ADC_RSQR1 = 0;                 /* one conversion in the sequence */
    CH32_ADC_RSQR3 = channel;
    CH32_ADC_CTLR2 |= CH32_ADC_CTLR2_SWSTART;
    while ((CH32_ADC_STATR & CH32_ADC_STATR_EOC) == 0u) {
    }
    uint32_t value = CH32_ADC_RDATAR & ((1u << CH32_ADC_BITS) - 1u);

    /* Scale the hardware's native width to the requested one, both ways. */
    if (ch32_adc_read_bits > CH32_ADC_BITS) {
        value <<= (ch32_adc_read_bits - CH32_ADC_BITS);
    } else if (ch32_adc_read_bits < CH32_ADC_BITS) {
        value >>= (CH32_ADC_BITS - ch32_adc_read_bits);
    }
    return (int)value;
#else
    (void)pin;
    return 0;
#endif
}
