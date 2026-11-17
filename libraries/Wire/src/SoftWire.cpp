#include "SoftWire.h"

#include "Arduino.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_ticks.h"
#include "softwire_lines.h"

namespace arduino {

static inline void line_write(uint8_t pin, bool high) { softwire_line_write(pin, high); }
static inline bool line_high(uint8_t pin) { return softwire_line_high(pin); }

/* I2C is open-drain: nobody ever drives the line high. LOW is a real drive,
 * HIGH is letting go and leaving it to the pull-up. CH32's GPIO has an
 * open-drain output mode, so both states come from one pin configuration and
 * the line can still be read while it is an output. */
void SoftWire::hold(uint8_t pin)
{
    line_write(pin, false);
}

void SoftWire::release(uint8_t pin)
{
    /* Whether it actually rose is not asked here: for SDA a low line is the
     * target answering, which is data, not an error. SCL is the one place a
     * stuck line matters, and scl_high() waits for it. */
    line_write(pin, true);
}

void SoftWire::half(void)
{
    /* Behind (the pin writes, SCL's rise, a stretch, an interrupt took longer
     * than the half): this half ends now and the next counts a whole half from
     * here. Never caught up by shortening a later half, so no half is shorter
     * than setClock()'s: the clock is never faster than asked. */
    const uint32_t now = ch32rv_ticks();
    _due_ticks += _half_ticks;
    if ((int32_t)(now - _due_ticks) >= 0) {
        _due_ticks = now;
        return;
    }
    while ((int32_t)(ch32rv_ticks() - _due_ticks) < 0) {
    }
}

/* Releasing SCL is a request, not a fact: a target is allowed to hold it down
 * until it is ready, and the transfer must wait. That is clock stretching, and
 * getting it wrong is the classic bit-banged-I2C bug - the controller marches
 * on and the target misses a bit. */
bool SoftWire::scl_high(void)
{
    const uint32_t start_us = micros();
    line_write(_scl, true);
    while (!line_high(_scl)) {
        if (_timeout_us != 0 && micros() - start_us > _timeout_us) {
            _timeout_flag = true;
            return false;
        }
    }
    return true;
}

void SoftWire::begin()
{
    /* No pins yet (a Software menu entry on a part with no default route):
     * begin(sda, scl) names them. */
    if (_sda == CH32RV_ROUTE_NO_PIN) {
        return;
    }
    if (_slave) {
        end();                         /* a target becomes a controller, as Wire's does */
    }
    setClock(_clock_hz);
    /* Both lines released. OUTPUT_OPENDRAIN before the write, so the pad never
     * drives high even briefly - on a bus with another controller, or a target
     * mid-transfer, a push-pull high is a short. */
    pinMode(_sda, OUTPUT_OPENDRAIN);
    pinMode(_scl, OUTPUT_OPENDRAIN);
    digitalWrite(_sda, HIGH);
    digitalWrite(_scl, HIGH);
    _tx_len = _rx_len = _rx_read = 0;
    _transmitting = _tx_overflow = false;
    _started = true;
}

bool SoftWire::clearBus(void)
{
    if (_sda == CH32RV_ROUTE_NO_PIN) {
        return false;
    }
    if (!_started) {
        pinMode(_sda, OUTPUT_OPENDRAIN);
        pinMode(_scl, OUTPUT_OPENDRAIN);
    }
    release(_sda);
    bool scl_free = scl_high();
    delayMicroseconds(5);
    /* 100 kHz half periods, which every target accepts. The target shifts out
     * the rest of its byte, sees no ACK, and lets go. */
    for (uint8_t pulses = 0; scl_free && pulses < 9 && digitalRead(_sda) == LOW; pulses++) {
        hold(_scl);
        delayMicroseconds(5);
        scl_free = scl_high();
        delayMicroseconds(5);
    }
    /* STOP: SDA rises while SCL is high. */
    hold(_scl);
    delayMicroseconds(2);
    hold(_sda);
    delayMicroseconds(3);
    scl_free = scl_high() && scl_free;
    delayMicroseconds(5);
    release(_sda);
    delayMicroseconds(5);
    const bool free = scl_free && digitalRead(_sda) == HIGH && digitalRead(_scl) == HIGH;
    if (!_started) {
        pinMode(_sda, INPUT);
        pinMode(_scl, INPUT);
    }
    return free;
}

/* Set by begin(address) (SoftWireTarget.cpp): how end() lets the target's
 * lines go. A pointer rather than a call, so a controller-only sketch does not
 * link detachInterrupt() and the EXTI handlers through end(). */
void (*softwire_target_stop)(SoftWire *bus);

void SoftWire::end()
{
    if (!_started) {
        return;
    }
    if (_slave && softwire_target_stop) {
        softwire_target_stop(this);
    }
    pinMode(_sda, INPUT);
    pinMode(_scl, INPUT);
    _started = false;
}

bool SoftWire::setPins(int sda, int scl)
{
    if (sda < 0 || scl < 0) {
        return false;
    }
    /* Pads from here on, so 0 and its pad name compare equal and every bit
     * toggled later skips the board-number lookup. */
    sda = CH32RV_PIN_RESOLVE(sda);
    scl = CH32RV_PIN_RESOLVE(scl);
    if (sda == scl || !ch32rv_pad_is_valid(sda) || !ch32rv_pad_is_valid(scl)) {
        return false;
    }
    const bool was_started = _started;
    end();
    _sda = (uint8_t)sda;
    _scl = (uint8_t)scl;
    if (was_started) {
        begin();
    }
    return true;
}


bool SoftWire::setClock(uint32_t freq)
{
    if (freq == 0) {
        return false;
    }
    _clock_hz = freq;
    /* Half a period in ticks, rounded up so a clock that cannot be met
     * exactly errs slow: the caller asked for a limit. Over half the tick
     * rate a half is one tick, the fastest there is (and 2 x freq would
     * overflow on the way: 2^31 divided by zero). */
    const uint32_t hz = (uint32_t)CH32RV_SYSTICK_HZ;
    _half_ticks = freq >= hz / 2u ? 1u : (hz + 2u * freq - 1u) / (2u * freq);
    return true;
}


void SoftWire::start_condition(void)
{
    /* SDA falls while SCL is high. Both are released first so that a repeated
     * start works from wherever the previous byte left the lines. */
    _due_ticks = ch32rv_ticks();
    release(_sda);
    half();
    scl_high();
    half();
    /* SDA low with SCL high: a target cut off in the middle of a byte it
     * sends is holding it. Clock it out - up to 9 pulses, a byte and its
     * ACK, until it lets go (it sees no ACK) - as the I2C specification's
     * bus clear does; Wire's controller does the same after a failed
     * transfer. Without it the address went out as the clocks of that byte
     * and the target answered whatever came next. */
    for (uint8_t n = 0; n < 9 && !line_high(_sda); n++) {
        hold(_scl);
        half();
        scl_high();
        half();
    }
    hold(_sda);
    half();
    hold(_scl);
    half();
}

void SoftWire::stop_condition(void)
{
    /* SDA rises while SCL is high - the mirror image of the start. */
    hold(_sda);
    half();
    scl_high();
    half();
    release(_sda);
    half();
}

bool SoftWire::write_byte(uint8_t b)
{
    for (uint8_t i = 0; i < 8; i++) {
        if (b & 0x80u) {
            release(_sda);
        } else {
            hold(_sda);
        }
        b = (uint8_t)(b << 1);
        half();
        if (!scl_high()) {
            return false;
        }
        half();
        hold(_scl);
    }
    /* The ninth clock is the target's. Let SDA go so it can pull it down. */
    release(_sda);
    half();
    if (!scl_high()) {
        return false;
    }
    const bool ack = !line_high(_sda);
    half();
    hold(_scl);
    return ack;
}

uint8_t SoftWire::read_byte(bool ack)
{
    uint8_t b = 0;
    release(_sda);
    for (uint8_t i = 0; i < 8; i++) {
        half();
        if (!scl_high()) {
            return b;
        }
        b = (uint8_t)(b << 1);
        if (line_high(_sda)) {
            b |= 1u;
        }
        half();
        hold(_scl);
    }
    /* ACK asks for another byte; NACK tells the target to stop talking, and
     * has to be sent before the last byte's stop. */
    if (ack) {
        hold(_sda);
    } else {
        release(_sda);
    }
    half();
    scl_high();
    half();
    hold(_scl);
    release(_sda);
    return b;
}




/* The address after a START, false when it went unacknowledged. A 10-bit
 * one is Wire10Bit.cpp's, there when the sketch named WIRE_10BIT. */
bool SoftWire::send_address(uint16_t address, bool read)
{
    if ((address & CH32RV_WIRE_10BIT_BIT) && softwire_send10) {
        return softwire_send10(*this, address, read);
    }
    return write_byte((uint8_t)(((address & 0x7Fu) << 1) | (read ? 1u : 0u)));
}

uint8_t SoftWire::transmit(bool stopBit)
{
    if (_slave) {
        return 4;                      /* a target: one role at a time */
    }
    start_condition();
    if (!send_address(_address, false)) {
        stop_condition();
        return _timeout_flag ? 5 : 2;  /* 2 = nothing at that address */
    }
    for (uint8_t i = 0; i < _tx_len; i++) {
        if (!write_byte(_tx[i])) {
            stop_condition();
            return _timeout_flag ? 5 : 3;   /* 3 = target stopped taking data */
        }
    }
    if (stopBit) {
        stop_condition();
    }
    return 0;
}

size_t SoftWire::receive(uint16_t address, size_t len, bool stopBit)
{
    if (_slave) {
        return 0;
    }
    start_condition();
    if (!send_address(address, true)) {
        stop_condition();
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        /* NACK the last byte: an ACK would ask for one more. */
        _rx[i] = read_byte(i + 1 < len);
    }
    if (stopBit) {
        stop_condition();
    }
    return len;
}




}  // namespace arduino
