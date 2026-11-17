/* CH32RVAdcStream's triggered runs: beginTimer() and beginExternal(), on the
 * core's trigger sources (internal/ch32rv_adc_trigger.h: the family's timers
 * and trigger pad). The library is an archive and this file is reached only
 * through these two (and triggerTimer()): CH32RVAdcStream.cpp's
 * triggerControl() is a weak default this file's replaces, so a sketch that
 * only free-runs links none of it. */
#include "CH32RVAdcStream.h"

#include "internal/ch32rv_adc_run.h"

/* One stream's timer at a time: there is one ADC1. */
static ch32rv_adc_timer_trigger_t stream_timer;
static uint8_t stream_extsel;

enum : uint8_t { FREE = 0, TIMER = 1, PAD = 2 };

bool CH32RVAdcStream::triggerControl(uint8_t op)
{
    if (_trigger == FREE) {
        return false;
    }
    if (op == TRIGGER_START) {
        ch32rv_adc_arm_triggered(stream_extsel);
        if (_trigger == TIMER) {
            ch32rv_adc_timer_trigger_start(&stream_timer);
        }
    } else if (op == TRIGGER_STOP) {
        if (_trigger == TIMER) {
            ch32rv_adc_timer_trigger_stop(&stream_timer);
        }
        ch32rv_adc_halt_triggered();
    } else {
        if (_trigger == TIMER) {
            ch32rv_adc_timer_trigger_end(&stream_timer);
        } else {
            ch32rv_adc_pin_trigger_end(_triggerPin);
        }
        _trigger = FREE;
    }
    return true;
}

uint8_t CH32RVAdcStream::triggerTimer() const
{
    return _trigger == TIMER ? stream_timer.timer : 0u;
}

/* The longest step whose free-running rounds come at least 1.25 times as
 * fast as `rate`: a round then fits in 80 % of a trigger's period. The
 * shortest when none does (the ADC then misses triggers, as asked). */
uint8_t CH32RVAdcStream::longestStep(uint32_t rate)
{
    const uint64_t want = (uint64_t)rate + rate / 4u;
    for (int8_t step = 7; step > 0; step--) {
        if (measure((uint8_t)step) >= want) {
            return (uint8_t)step;
        }
    }
    return 0;
}

bool CH32RVAdcStream::beginTimer(pin_size_t pin, uint32_t rate)
{
    return beginTimer(&pin, 1, rate);
}

bool CH32RVAdcStream::beginTimer(const pin_size_t *pins, uint8_t count, uint32_t rate)
{
    /* begin() sets up the pins and the ADC; asked for the highest rate it
     * measures the first step only. */
    if (!begin(pins, count, 0xFFFFFFFFu) || !ch32rv_adc_timer_trigger_begin(&stream_timer, rate, 1)) {
        end();
        return false;
    }
    _trigger = TIMER;
    stream_extsel = stream_timer.extsel;
    _rate = (ch32rv_adc_timer_trigger_millihz(&stream_timer) + 500u) / 1000u;
    _step = longestStep(rate);
    configure(_step);
    return true;
}

bool CH32RVAdcStream::beginExternal(pin_size_t pin, pin_size_t triggerPin, PinStatus edge, uint32_t maxRate)
{
    return beginExternal(&pin, 1, triggerPin, edge, maxRate);
}

bool CH32RVAdcStream::beginExternal(const pin_size_t *pins, uint8_t count, pin_size_t triggerPin,
                                    PinStatus edge, uint32_t maxRate)
{
    if (!ch32rv_adc_pin_trigger_valid(triggerPin) || (edge != RISING && edge != FALLING && edge != CHANGE) ||
        !begin(pins, count, 0xFFFFFFFFu)) {
        return false;
    }
    if (!ch32rv_adc_pin_trigger_begin(triggerPin, (uint8_t)edge, &stream_extsel)) {
        end();
        return false;
    }
    _trigger = PAD;
    _triggerPin = triggerPin;
    _rate = 0;
    _step = maxRate ? longestStep(maxRate) : 7u;
    configure(_step);
    return true;
}
