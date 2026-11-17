/* CH32RVAdcStream: ADC1 in continuous (and, with several pins, scan) mode, or
 * one round per trigger, on the core's ADC run (internal/ch32rv_adc_run.h:
 * the DMA channel, its interrupt, the stops that leave nothing pending) and
 * trigger sources (internal/ch32rv_adc_trigger.h: the family's timers and
 * trigger pad). */
#include "CH32RVAdcStream.h"

#include "internal/ch32rv_adc_run.h"

bool CH32RVAdcStream::begin(pin_size_t pin, uint32_t rate)
{
    return begin(&pin, 1, rate);
}

bool CH32RVAdcStream::prepare(const pin_size_t *pins, uint8_t count)
{
    if (!pins || count == 0 || count > MAX_PINS) {
        return false;
    }
    end();
    if (!ch32rv_adc_run_channels(pins, count, _channels)) {
        return false;
    }
    _count = count;
    _rate = 0;
    return true;
}

bool CH32RVAdcStream::begin(const pin_size_t *pins, uint8_t count, uint32_t rate)
{
    if (rate == 0 || !prepare(pins, count)) {
        return false;
    }
    /* Fastest first: the first step at or below the rate is the one. */
    _step = 7;
    for (uint8_t step = 0; step < 8; step++) {
        const uint32_t got = ch32rv_adc_run_measure(_channels, _count, step);
        if (got == 0) {
            continue;
        }
        if (got <= rate || step == 7) {
            _step = step;
            _rate = got;
            break;
        }
    }
    if (_rate == 0) {
        return false;
    }
    ch32rv_adc_run_sequence(_channels, _count, _step);
    _begun = true;
    return true;
}

/* The longest step whose free-running rounds come at least 1.25 times as
 * fast as `rate`: a round then fits in 80 % of a trigger's period. The
 * shortest when none does (the ADC then misses triggers, as asked). */
uint8_t CH32RVAdcStream::longestStep(uint32_t rate)
{
    const uint64_t want = (uint64_t)rate + rate / 4u;
    for (int8_t step = 7; step > 0; step--) {
        if (ch32rv_adc_run_measure(_channels, _count, (uint8_t)step) >= want) {
            return (uint8_t)step;
        }
    }
    return 0;
}

bool CH32RVAdcStream::beginTimer(pin_size_t pin, uint32_t rate)
{
    return beginTimer(&pin, 1, rate);
}

/* One stream's timer at a time: there is one ADC1. */
static ch32rv_adc_timer_trigger_t stream_timer;

void CH32RVAdcStream::timerControl(CH32RVAdcStream *self, uint8_t op)
{
    if (op == TRIGGER_START) {
        ch32rv_adc_run_start_triggered(self->_buf, (uint32_t)self->_samples * 2u, self->_trigger, handler, self);
        ch32rv_adc_timer_trigger_start(&stream_timer);
    } else if (op == TRIGGER_STOP) {
        ch32rv_adc_timer_trigger_stop(&stream_timer);
        ch32rv_adc_run_stop_triggered();
    } else {
        ch32rv_adc_timer_trigger_end(&stream_timer);
    }
}

uint8_t CH32RVAdcStream::triggerTimer() const
{
    return _control == timerControl ? stream_timer.timer : 0u;
}

bool CH32RVAdcStream::beginTimer(const pin_size_t *pins, uint8_t count, uint32_t rate)
{
    if (!prepare(pins, count) || !ch32rv_adc_timer_trigger_begin(&stream_timer, rate, 1)) {
        return false;
    }
    _control = timerControl;
    _trigger = stream_timer.extsel;
    _rate = (ch32rv_adc_timer_trigger_millihz(&stream_timer) + 500u) / 1000u;
    _step = longestStep(rate);
    ch32rv_adc_run_sequence(_channels, _count, _step);
    _begun = true;
    return true;
}

void CH32RVAdcStream::pinControl(CH32RVAdcStream *self, uint8_t op)
{
    if (op == TRIGGER_START) {
        ch32rv_adc_run_start_triggered(self->_buf, (uint32_t)self->_samples * 2u, self->_trigger, handler, self);
    } else if (op == TRIGGER_STOP) {
        ch32rv_adc_run_stop_triggered();
    } else {
        ch32rv_adc_pin_trigger_end(self->_triggerPin);
    }
}

bool CH32RVAdcStream::beginExternal(pin_size_t pin, pin_size_t triggerPin, PinStatus edge, uint32_t maxRate)
{
    return beginExternal(&pin, 1, triggerPin, edge, maxRate);
}

bool CH32RVAdcStream::beginExternal(const pin_size_t *pins, uint8_t count, pin_size_t triggerPin,
                                    PinStatus edge, uint32_t maxRate)
{
    if (!ch32rv_adc_pin_trigger_valid(triggerPin) || (edge != RISING && edge != FALLING && edge != CHANGE) ||
        !prepare(pins, count)) {
        return false;
    }
    uint8_t extsel = 0;
    if (!ch32rv_adc_pin_trigger_begin(triggerPin, (uint8_t)edge, &extsel)) {
        return false;
    }
    _trigger = extsel;
    _triggerPin = triggerPin;
    _control = pinControl;
    _step = maxRate ? longestStep(maxRate) : 7u;
    ch32rv_adc_run_sequence(_channels, _count, _step);
    _begun = true;
    return true;
}

void CH32RVAdcStream::end()
{
    stop();
    if (_control) {
        _control(this, TRIGGER_END);
        _control = nullptr;
    }
    if (!_begun) {
        return;
    }
    ch32rv_adc_run_restore();
    _begun = false;
}

bool CH32RVAdcStream::startContinuous(uint16_t *buf0, uint16_t *buf1, uint16_t samples)
{
    if (buf1 != buf0 + samples) {
        return false;
    }
    return startContinuous(buf0, samples);
}

bool CH32RVAdcStream::startContinuous(uint16_t *buffer, uint16_t samples)
{
    if (!_begun || !buffer || samples < _count || samples % _count != 0 || samples > 32767u) {
        return false;
    }
    stop();
    _buf = buffer;
    _samples = samples;
    _ready = 0;
    _last = 0;
    _overruns = _filled = 0;
    ch32rv_adc_run_sequence(_channels, _count, _step);
    _running = true;
    if (_control) {
        _control(this, TRIGGER_START);   /* the triggered run, then its source */
    } else {
        ch32rv_adc_run_start(buffer, (uint32_t)samples * 2u, handler, this);
    }
    return true;
}

void CH32RVAdcStream::stop()
{
    if (!_running) {
        return;
    }
    if (_control) {
        _control(this, TRIGGER_STOP);   /* its source, then the triggered run */
    } else {
        ch32rv_adc_run_stop();
    }
    _running = false;
}

const uint16_t *CH32RVAdcStream::readBuffer()
{
    noInterrupts();
    const uint8_t r = _ready;
    uint8_t take = 0xFF;
    if (r) {
        /* The older one first when both are waiting (the newer is then
         * being overwritten - already counted). */
        take = (r == 3u) ? (uint8_t)(_last ^ 1u) : (uint8_t)(r == 1u ? 0u : 1u);
        _ready = (uint8_t)(r & ~(1u << take));
    }
    interrupts();
    __asm__ volatile("fence rw, rw" ::: "memory");
    return take == 0xFF ? nullptr : _buf + (size_t)take * _samples;
}

uint32_t CH32RVAdcStream::conversions() const
{
    if (!_running || !_count) {
        return 0;
    }
    const uint32_t total = (uint32_t)_samples * 2u;
    uint32_t laps, remaining, pending;
    /* The counter and the wrap's flag read together: a wrap between them
     * would count its lap twice or not at all. */
    do {
        noInterrupts();
        pending = ch32rv_adc_run_pending() & CH32RV_ADC_RUN_FULL;
        laps = _filled / 2u;             /* halves alternate: every second one ends a lap */
        remaining = ch32rv_adc_run_remaining();
        const uint32_t again = ch32rv_adc_run_pending() & CH32RV_ADC_RUN_FULL;
        interrupts();
        if (again == pending) {
            break;
        }
    } while (true);
    if (pending) {
        laps++;                          /* wrapped, its interrupt still to run */
    }
    const uint32_t written = remaining >= total ? 0u : total - remaining;
    return (laps * total + written) / _count;
}

void CH32RVAdcStream::handler(void *self, uint32_t flags)
{
    CH32RVAdcStream *s = static_cast<CH32RVAdcStream *>(self);
    /* Half-transfer: the first half is full; transfer-complete: the second.
     * Both at once (an interrupt held off for a whole half) - first, then second. */
    for (uint8_t half = 0; half < 2; half++) {
        if (!(flags & (half == 0 ? CH32RV_ADC_RUN_HALF : CH32RV_ADC_RUN_FULL))) {
            continue;
        }
        if (s->_ready & (1u << half)) {
            s->_overruns++;              /* the sketch still had not taken it */
        }
        s->_ready |= (uint8_t)(1u << half);
        s->_last = half;
        s->_filled++;
    }
}
