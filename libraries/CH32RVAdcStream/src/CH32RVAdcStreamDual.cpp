/* CH32RVAdcStream's dual mode: ADC2 alongside ADC1 (CH32FV2x_V3xRM 12.3.x
 * "Dual mode" and the same chapter of the V407 / X315 / H417 manuals). ADC1
 * is the master: its trigger starts both, and its RDATAR carries ADC2's
 * result in the upper half, so the stream's one DMA channel moves words
 * instead of half-words. ADC2 is set to the software trigger as the manual
 * asks of the slave. The DUALMOD values are device-data's
 * (adc_dual_modes.csv, CH32RV_ADC_DUAL_*); linked only by a sketch that calls
 * simultaneous() or beginInterleaved(). */
#include "CH32RVAdcStream.h"

#include "internal/ch32rv_adc_run.h"

#ifdef CH32RV_ADC_DUALMOD_SHIFT

#define ADC2_RSQR2 CH32RV_REG32(CH32RV_ADC2_BASE + 0x30u)
#define ADC2_MODE  (CH32RV_ADC_CTLR2_EXTSEL_SWSTART | CH32RV_ADC_CTLR2_EXTTRIG)

/* ADC2 powered down and up: whatever it was converting is dropped, and the
 * power-up starts nothing (ch32rv_adc_restart's finding on ADC1). */
static void adc2_restart(uint32_t extra)
{
    CH32RV_ADC_CTLR2_AT(CH32RV_ADC2_BASE) = ADC2_MODE;
    CH32RV_ADC_CTLR2_AT(CH32RV_ADC2_BASE) = ADC2_MODE | extra | CH32RV_ADC_CTLR2_ADON;
    for (volatile int settle = 0; settle < 1000; settle++) {
    }
    (void)CH32RV_ADC_RDATAR_AT(CH32RV_ADC2_BASE);
}

bool CH32RVAdcStream::simultaneous(const pin_size_t *pins2, uint8_t count)
{
    if (!_begun || _running || !pins2 || count != _count) {
        return false;
    }
    uint8_t channels[MAX_PINS];
    for (uint8_t i = 0; i < count; i++) {
        const pin_size_t pad = CH32RV_PIN_RESOLVE(pins2[i]);
        const unsigned channel = CH32RV_PIN_TO_ADC_CHANNEL(pad);
        if (channel == NOT_AN_ANALOG_PIN) {
            return false;
        }
#ifdef CH32RV_PIN_TO_ADC_INSTANCE
        if (CH32RV_PIN_TO_ADC_INSTANCE(pad) != 2) {
            return false;                    /* X305 / X315: ADC2's own pads */
        }
#else
        if (channel == _channels[i]) {
            return false;                    /* one channel on both at once: the manual forbids it */
        }
#endif
        channels[i] = (uint8_t)channel;
        /* The pad in analog mode (on X305 / X315 ADC2 started too); ADC1's
         * set-up that this disturbs is configure()'s again below. */
        (void)analogRead(pins2[i]);
    }
    ch32rv_adc2_begin();
    for (uint8_t i = 0; i < count; i++) {
        _channels2[i] = channels[i];
    }
    _dual = DUAL_SIMULTANEOUS;
    configure(_step);
    return true;
}

bool CH32RVAdcStream::beginInterleaved(pin_size_t pin)
{
#ifdef CH32RV_PIN_TO_ADC_INSTANCE
    (void)pin;
    return false;                            /* X305 / X315: no pad reaches both ADCs */
#else
    /* The shortest sample time: the manual asks for less than the 7 ADC
     * clocks between the two starts. */
    if (!begin(pin, 0xFFFFFFFFu) || _step != 0) {
        end();
        return false;
    }
    ch32rv_adc2_begin();
    _channels2[0] = _channels[0];
    _dual = DUAL_INTERLEAVED;
    _rate *= 2u;
    configure(_step);
    return true;
#endif
}

void CH32RVAdcStream::dualTake(uint16_t *half)
{
    if (_dual != DUAL_INTERLEAVED) {
        return;
    }
    /* Each word is ADC1's value under ADC2's, and ADC2 converted first:
     * swapped into time order. */
    uint32_t *word = (uint32_t *)half;
    for (uint16_t i = 0; i < _samples / 2u; i++) {
        word[i] = (word[i] >> 16) | (word[i] << 16);
    }
}

void CH32RVAdcStream::dualControl(uint8_t op)
{
    if (_dual == DUAL_OFF) {
        return;
    }
    switch (op) {
    case DUAL_CONFIGURE: {
        /* ADC2's sequence and sample times as ADC1's; DUALMOD in ADC1's
         * CTLR1, which configure() has just rewritten. */
        uint32_t smpr1 = CH32RV_ADC_SAMPTR1_AT(CH32RV_ADC2_BASE), smpr2 = CH32RV_ADC_SAMPTR2_AT(CH32RV_ADC2_BASE);
        uint32_t sq = 0;
        for (uint8_t i = 0; i < _count; i++) {
            const uint8_t c = _channels2[i];
            if (c < 10u) {
                smpr2 = (smpr2 & ~(7u << (3u * c))) | ((uint32_t)_step << (3u * c));
            } else {
                smpr1 = (smpr1 & ~(7u << (3u * (c - 10u)))) | ((uint32_t)_step << (3u * (c - 10u)));
            }
            sq |= (uint32_t)c << (5u * i);
        }
        CH32RV_ADC_SAMPTR1_AT(CH32RV_ADC2_BASE) = smpr1;
        CH32RV_ADC_SAMPTR2_AT(CH32RV_ADC2_BASE) = smpr2;
        CH32RV_ADC_RSQR3_AT(CH32RV_ADC2_BASE) = sq;
        ADC2_RSQR2 = 0;
        CH32RV_ADC_RSQR1_AT(CH32RV_ADC2_BASE) = (uint32_t)(_count - 1u) << 20;
        CH32RV_ADC_CTLR1_AT(CH32RV_ADC2_BASE) = (_count > 1 ? CH32RV_ADC_CTLR1_SCAN : 0u) | CH32RV_ADC_CTLR1_RESET;
        const uint32_t mode = _dual == DUAL_SIMULTANEOUS ? CH32RV_ADC_DUAL_REGULAR_SIMULTANEOUS
                                                         : CH32RV_ADC_DUAL_FAST_INTERLEAVED;
        CH32RV_ADC_CTLR1 = (CH32RV_ADC_CTLR1 & ~(0xFu << CH32RV_ADC_DUALMOD_SHIFT)) |
                           (mode << CH32RV_ADC_DUALMOD_SHIFT);
        break;
    }
    case DUAL_START:
        /* Waiting for ADC1's start; free-running, it goes on by itself as
         * ADC1 does. */
        adc2_restart(CH32RV_ADC_CTLR2_DMA | (_trigger == 0 ? CH32RV_ADC_CTLR2_CONT : 0u));
        break;
    case DUAL_STOP:
        adc2_restart(0);
        break;
    case DUAL_END:
        adc2_restart(0);
        CH32RV_ADC_CTLR1_AT(CH32RV_ADC2_BASE) = CH32RV_ADC_CTLR1_RESET;
        CH32RV_ADC_RSQR1_AT(CH32RV_ADC2_BASE) = 0;
        ADC2_RSQR2 = 0;
        _dual = DUAL_OFF;
        break;
    }
}

#else

bool CH32RVAdcStream::simultaneous(const pin_size_t *pins2, uint8_t count)
{
    (void)pins2;
    (void)count;
    return false;
}

bool CH32RVAdcStream::beginInterleaved(pin_size_t pin)
{
    (void)pin;
    return false;
}

#endif
