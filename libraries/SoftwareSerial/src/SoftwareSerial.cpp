/* SoftwareSerial (SoftwareSerial.h). Timing comes from SysTick through the
 * core's ch32rv_ticks(), the start bit's interrupt and its delay through
 * attachInterruptArg() and ch32rv_exti.h: nothing here depends on the chip. */
#include "SoftwareSerial.h"

#include "internal/ch32rv_exti.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_ticks.h"

SoftwareSerial *SoftwareSerial::active = nullptr;
CH32RVRingBuffer<_SS_MAX_RX_BUFF> SoftwareSerial::buffer;
volatile bool SoftwareSerial::_overflow = false;

namespace {

constexpr uint8_t NO_PIN = 0xFF;

uint8_t pad_of(uint8_t pin)
{
    if (pin == NO_PIN) {
        return NO_PIN;
    }
    const uint8_t pad = (uint8_t)CH32RV_PIN_RESOLVE(pin);
    return ch32rv_pad_is_valid(pad) ? pad : NO_PIN;
}

inline bool level(uint8_t pad)
{
    return ch32rv_gpio_read((uint8_t)CH32RV_PIN_PORT(pad), (uint8_t)CH32RV_PIN_BIT(pad)) != 0;
}

/* Until SysTick passes `at` (wraps as int32_t differences do). */
inline void wait_until(uint32_t at)
{
    while ((int32_t)(ch32rv_ticks() - at) < 0) {
    }
}

void rx_isr(void *self)
{
    static_cast<SoftwareSerial *>(self)->recv();
}

}  // namespace

SoftwareSerial::SoftwareSerial(uint8_t receivePin, uint8_t transmitPin, bool inverse_logic)
    : _rx(pad_of(receivePin)), _tx(pad_of(transmitPin)), _inverse(inverse_logic)
{
}

SoftwareSerial::~SoftwareSerial()
{
    end();
}

void SoftwareSerial::begin(long speed, uint16_t config)
{
    end();
    _bit_q4 = 0;
    const uint16_t data = config & SERIAL_DATA_MASK;
    if (speed <= 0 || data < SERIAL_DATA_5 || data > SERIAL_DATA_8) {
        return;
    }
    _bits = (uint8_t)(4u + (data >> 8));
    switch (config & SERIAL_PARITY_MASK) {
    case SERIAL_PARITY_EVEN: _parity = 1; break;
    case SERIAL_PARITY_ODD: _parity = 2; break;
    case SERIAL_PARITY_MARK: _parity = 3; break;
    case SERIAL_PARITY_SPACE: _parity = 4; break;
    default: _parity = 0; break;
    }
    _bit_q4 = (uint32_t)(((uint64_t)CH32RV_SYSTICK_HZ * 16u + (uint32_t)speed / 2u) / (uint32_t)speed);
    switch (config & SERIAL_STOP_BIT_MASK) {
    case SERIAL_STOP_BIT_2: _stop_q4 = _bit_q4 * 2u; break;
    case SERIAL_STOP_BIT_1_5: _stop_q4 = _bit_q4 * 3u / 2u; break;
    default: _stop_q4 = _bit_q4; break;
    }
    if (_tx != NO_PIN) {
        /* Idle before it drives - with the port's clock on, or the write is
         * lost and the pad starts low (the first begin() on a port nothing
         * had used: the first frame broke on every bench part, 2026-11-19). */
        ch32rv_gpio_clock_enable((uint8_t)CH32RV_PIN_PORT(_tx));
        sendBit(true);
        pinMode(_tx, OUTPUT);
    }
    if (_rx != NO_PIN) {
        /* Pulled to idle, as AVR does: a receiver with nothing connected
         * sees no start bits. */
        pinMode(_rx, _inverse ? INPUT_PULLDOWN : INPUT_PULLUP);
        listen();
    }
}

void SoftwareSerial::end()
{
    stopListening();
}

bool SoftwareSerial::listen()
{
    if (_rx == NO_PIN || !_bit_q4 || active == this) {
        return false;
    }
    if (active) {
        active->stopListening();
    }
    buffer.clear();
    _overflow = false;
    active = this;
    /* How long the start bit's interrupt takes to arrive, measured with a
     * software edge on the same line (its path is the same from the pending
     * flag on); the least of a few, as another interrupt can only make one
     * longer. Each frame's bit times are counted from the edge so found. */
    attachInterruptArg(_rx, rx_isr, this, _inverse ? RISING : FALLING);
    uint32_t least = UINT32_MAX;
    for (uint8_t i = 0; i < 4; i++) {
        _calibrating = true;
        const uint32_t from = ch32rv_ticks();
        ch32rv_exti_trigger(_rx);
        const uint32_t limit = CH32RV_SYSTICK_HZ / 1000u;  /* 1 ms */
        while (_calibrating && ch32rv_ticks() - from < limit) {
        }
        if (!_calibrating && _calibrated_at - from < least) {
            least = _calibrated_at - from;
        }
    }
    _calibrating = false;
    _latency = least == UINT32_MAX ? 0 : least;
    ch32rv_exti_clear(_rx);
    return true;
}

bool SoftwareSerial::stopListening()
{
    if (active != this) {
        return false;
    }
    detachInterrupt(_rx);
    active = nullptr;
    return true;
}

void SoftwareSerial::recv()
{
    const uint32_t now = ch32rv_ticks();
    if (_calibrating) {
        _calibrated_at = now;
        _calibrating = false;
        return;
    }
    const uint8_t pad = _rx;
    const bool idle = !_inverse;
    /* A start bit is still on the line: otherwise this was a glitch, or an
     * edge from before (inside a frame the transmit side held up). */
    if (level(pad) == idle) {
        return;
    }
    /* No other interrupt inside the frame: it would move the samples. */
    const uint32_t irq = ch32rv_irq_save();
    uint32_t edge = now - _latency;
    for (;;) {
        uint32_t pos = _bit_q4 + _bit_q4 / 2u;             /* the middle of data bit 0 */
        uint8_t data = 0;
        uint8_t ones = 0;
        for (uint8_t k = 0; k < _bits; k++) {
            wait_until(edge + (pos >> 4));
            if (level(pad) != _inverse) {
                data |= (uint8_t)(1u << k);
                ++ones;
            }
            pos += _bit_q4;
        }
        bool good = true;
        if (_parity) {
            wait_until(edge + (pos >> 4));
            const bool bit = level(pad) != _inverse;
            const bool want = _parity == 1 ? (ones & 1u) : _parity == 2 ? !(ones & 1u) : _parity == 3;
            good = bit == want;
            pos += _bit_q4;
        }
        wait_until(edge + (pos >> 4));
        good = good && (level(pad) == idle);                /* the stop bit */
        if (good && !buffer.push(data)) {
            _overflow = true;
        }
        /* Back-to-back frames: the next start bit comes half a bit after
         * this sample, sooner than a fresh interrupt arrives on the slow
         * parts (X035 / V006 at 115200 lost every other byte, 2026-11-17).
         * So the line is watched until a bit after the stop bit's end, and a
         * start bit seen there is timed here, exactly, and read in this same
         * interrupt. */
        const uint32_t until = edge + ((pos - _bit_q4 / 2u + _stop_q4 + _bit_q4) >> 4);
        bool next = false;
        /* Not after a bad stop bit: a line held low (a break) would be read
         * as frame after frame, never leaving the interrupt. */
        while (good && (int32_t)(ch32rv_ticks() - until) < 0) {
            if (level(pad) != idle) {
                edge = ch32rv_ticks();
                next = true;
                break;
            }
        }
        if (!next) {
            break;
        }
    }
    /* The data's edges set the line's pending flag: they are no start bit. */
    ch32rv_exti_clear(pad);
    ch32rv_irq_restore(irq);
}

void SoftwareSerial::sendBit(bool high) const
{
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(_tx), bit = (uint8_t)CH32RV_PIN_BIT(_tx);
    if (high != _inverse) {
        ch32rv_gpio_set(port, bit);
    } else {
        ch32rv_gpio_clear(port, bit);
    }
}

size_t SoftwareSerial::write(uint8_t byte)
{
    if (_tx == NO_PIN || !_bit_q4) {
        setWriteError();
        return 0;
    }
    /* Start, data (LSB first), parity: one word, sent low bit first. */
    uint32_t frame = (uint32_t)(byte & ((1u << _bits) - 1u)) << 1;
    uint8_t count = (uint8_t)(1u + _bits);
    if (_parity) {
        const bool odd = __builtin_popcount(byte & ((1u << _bits) - 1u)) & 1;
        const bool bit = _parity == 1 ? odd : _parity == 2 ? !odd : _parity == 3;
        frame |= (uint32_t)bit << count;
        ++count;
    }
    const uint32_t irq = ch32rv_irq_save();
    const uint32_t start = ch32rv_ticks();
    uint32_t pos = 0;
    for (uint8_t i = 0; i < count; i++) {
        sendBit((frame >> i) & 1u);
        pos += _bit_q4;
        wait_until(start + (pos >> 4));
    }
    sendBit(true);
    /* What came in meanwhile is lost (half duplex): its edges are no start
     * bit for later. */
    if (active) {
        ch32rv_exti_clear(active->_rx);
    }
    ch32rv_irq_restore(irq);
    /* The stop bit with interrupts on: one coming now only lengthens it. */
    wait_until(start + ((pos + _stop_q4) >> 4));
    return 1;
}

int SoftwareSerial::read()
{
    return isListening() ? buffer.pop() : -1;
}

int SoftwareSerial::available()
{
    return isListening() ? buffer.available() : 0;
}

int SoftwareSerial::peek()
{
    return isListening() ? buffer.peek() : -1;
}
