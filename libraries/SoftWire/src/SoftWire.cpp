#include "SoftWire.h"

#include "Arduino.h"

namespace arduino {

/* I2C is open-drain: nobody ever drives the line high. LOW is a real drive,
 * HIGH is letting go and leaving it to the pull-up. CH32's GPIO has an
 * open-drain output mode, so both states come from one pin configuration and
 * the line can still be read while it is an output. */
void SoftWire::hold(uint8_t pin)
{
    digitalWrite(pin, LOW);
}

void SoftWire::release(uint8_t pin)
{
    /* Whether it actually rose is not asked here: for SDA a low line is the
     * target answering, which is data, not an error. SCL is the one place a
     * stuck line matters, and scl_high() waits for it. */
    digitalWrite(pin, HIGH);
}

void SoftWire::half(void)
{
    if (_half_us) {
        delayMicroseconds(_half_us);
    }
}

/* Releasing SCL is a request, not a fact: a target is allowed to hold it down
 * until it is ready, and the transfer must wait. That is clock stretching, and
 * getting it wrong is the classic bit-banged-I2C bug - the controller marches
 * on and the target misses a bit. */
bool SoftWire::scl_high(void)
{
    const uint32_t start_us = micros();
    digitalWrite(_scl, HIGH);
    while (digitalRead(_scl) == LOW) {
        if (_timeout_us != 0 && micros() - start_us > _timeout_us) {
            _timeout_flag = true;
            return false;
        }
    }
    return true;
}

void SoftWire::begin()
{
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

void SoftWire::end()
{
    if (!_started) {
        return;
    }
    pinMode(_sda, INPUT);
    pinMode(_scl, INPUT);
    _started = false;
}

void SoftWire::setClock(uint32_t freq)
{
    /* Half of one period, rounded down, and never zero for a named frequency:
     * the caller asked for a limit, so honour it as a limit. */
    if (freq == 0) {
        return;
    }
    const uint32_t half = 500000UL / freq;
    _half_us = (uint16_t)(half > 0xFFFFUL ? 0xFFFFUL : half);
}

void SoftWire::setWireTimeout(uint32_t timeout, bool reset_with_timeout)
{
    /* Accepted, not honoured: a timeout here always ends in a stop condition,
     * which is the whole of what "reset" can mean for a line pair. */
    (void)reset_with_timeout;
    _timeout_us = timeout;
}

void SoftWire::start_condition(void)
{
    /* SDA falls while SCL is high. Both are released first so that a repeated
     * start works from wherever the previous byte left the lines. */
    release(_sda);
    half();
    scl_high();
    half();
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
    const bool ack = digitalRead(_sda) == LOW;
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
        if (digitalRead(_sda) == HIGH) {
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

void SoftWire::beginTransmission(uint8_t address)
{
    _address = address;
    _tx_len = 0;
    _tx_overflow = false;
    _transmitting = true;
}

size_t SoftWire::write(uint8_t data)
{
    if (!_transmitting) {
        return 0;                      /* outside a transmission, as on AVR */
    }
    if (_tx_len >= SOFTWIRE_BUFFER_SIZE) {
        _tx_overflow = true;
        return 0;
    }
    _tx[_tx_len++] = data;
    return 1;
}

size_t SoftWire::write(const uint8_t *data, size_t len)
{
    size_t written = 0;
    while (len--) {
        if (write(*data++) == 0) {
            break;
        }
        written++;
    }
    return written;
}

uint8_t SoftWire::endTransmission(bool stopBit)
{
    _transmitting = false;
    if (_tx_overflow) {
        _tx_len = 0;
        return 1;                      /* AVR's "data too long" */
    }
    if (!_started) {
        begin();
    }

    start_condition();
    if (!write_byte((uint8_t)(_address << 1))) {
        stop_condition();
        _tx_len = 0;
        return _timeout_flag ? 5 : 2;  /* 2 = nothing at that address */
    }
    for (uint8_t i = 0; i < _tx_len; i++) {
        if (!write_byte(_tx[i])) {
            stop_condition();
            _tx_len = 0;
            return _timeout_flag ? 5 : 3;   /* 3 = target stopped taking data */
        }
    }
    if (stopBit) {
        stop_condition();
    }
    _tx_len = 0;
    return 0;
}

size_t SoftWire::requestFrom(uint8_t address, size_t len, bool stopBit)
{
    _rx_len = _rx_read = 0;
    if (!_started) {
        begin();
    }
    if (len > SOFTWIRE_BUFFER_SIZE) {
        len = SOFTWIRE_BUFFER_SIZE;
    }
    if (len == 0) {
        return 0;
    }

    start_condition();
    if (!write_byte((uint8_t)((address << 1) | 1u))) {
        stop_condition();
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        /* NACK the last byte: an ACK would ask for one more. */
        _rx[i] = read_byte(i + 1 < len);
        _rx_len++;
    }
    if (stopBit) {
        stop_condition();
    }
    return _rx_len;
}

int SoftWire::available(void)
{
    return _rx_len - _rx_read;
}

int SoftWire::read(void)
{
    if (_rx_read >= _rx_len) {
        return -1;
    }
    return _rx[_rx_read++];
}

int SoftWire::peek(void)
{
    if (_rx_read >= _rx_len) {
        return -1;
    }
    return _rx[_rx_read];
}

}  // namespace arduino
