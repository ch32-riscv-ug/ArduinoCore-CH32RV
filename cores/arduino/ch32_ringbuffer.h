/* Single-producer / single-consumer ring buffer for the serial driver.
 *
 * api/RingBuffer.h keeps one `_numElems` counter that both push and pop
 * read-modify-write. That is fine when one context owns the buffer, but a UART
 * has two: for TX the sketch pushes and the ISR pops, for RX the other way
 * round. The lost update corrupts the buffer - observed on CH32V203 as bytes
 * turning into spaces mid-line.
 *
 * Here the producer only ever writes `head` and the consumer only ever writes
 * `tail`, so neither needs a lock and no interrupt has to be disabled. The
 * cost is one unusable slot, which is what makes empty and full distinct.
 */
#pragma once

#include <stdint.h>

template <uint16_t N>
class CH32RingBuffer {
public:
    bool isEmpty(void) const { return _head == _tail; }
    bool isFull(void) const { return next(_head) == _tail; }

    /* Producer side. Drops the byte when full, like every Arduino core. */
    bool push(uint8_t c)
    {
        const uint16_t n = next(_head);
        if (n == _tail) {
            return false;
        }
        _buffer[_head] = c;
        _head = n;
        return true;
    }

    /* Consumer side. Returns -1 when empty, matching Stream::read(). */
    int pop(void)
    {
        if (_head == _tail) {
            return -1;
        }
        const uint8_t c = _buffer[_tail];
        _tail = next(_tail);
        return c;
    }

    int peek(void) const
    {
        return (_head == _tail) ? -1 : _buffer[_tail];
    }

    int available(void) const
    {
        return (int)((uint16_t)(_head - _tail) % N);
    }

    void clear(void) { _tail = _head; }

private:
    static uint16_t next(uint16_t i) { return (uint16_t)((i + 1u) % N); }

    uint8_t _buffer[N];
    volatile uint16_t _head = 0;
    volatile uint16_t _tail = 0;
};
