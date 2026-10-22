/* Ring<N> and MessageBuffer<N>: bytes rather than values.
 *
 *   Ring           a byte stream. write() takes what fits and says how much
 *                  (a partial write is normal: the caller keeps the rest);
 *                  read() returns what is there, up to the caller's size.
 *   MessageBuffer  whole messages of varying length. trySend() stores all of
 *                  a message or none of it; tryReceive() returns one whole
 *                  message, or nothing if the caller's buffer is too small
 *                  (nextLength() says how much room it needs).
 *
 * N is the storage in bytes. A message costs its length plus 2 bytes. */
#pragma once

#include <string.h>

#include "CH32RVSyncKitCommon.h"

namespace CH32RVSyncKit {

namespace detail {

/* The byte FIFO both use; not for sketches. Indices count bytes stored, so
 * all N bytes are usable (no empty slot). */
template <size_t N>
struct Bytes {
    static_assert(N > 0 && N <= 65535u, "1 to 65535 bytes");
    uint8_t data[N];
    uint16_t head = 0;
    uint16_t used = 0;

    size_t space() const { return N - used; }

    void put(const uint8_t *src, size_t n)
    {
        size_t at = head + used;
        if (at >= N) {
            at -= N;
        }
        const size_t first = n < N - at ? n : N - at;
        memcpy(data + at, src, first);
        memcpy(data, src + first, n - first);
        used = (uint16_t)(used + n);
    }

    void peek(uint8_t *dst, size_t n, size_t skip = 0) const
    {
        size_t at = head + skip;
        if (at >= N) {
            at -= N;
        }
        const size_t first = n < N - at ? n : N - at;
        memcpy(dst, data + at, first);
        memcpy(dst + first, data, n - first);
    }

    void drop(size_t n)
    {
        head = (uint16_t)(head + n >= N ? head + n - N : head + n);
        used = (uint16_t)(used - n);
    }
};

}  // namespace detail

template <size_t N, typename Sharing = IsrSafe>
class Ring {
public:
    static constexpr size_t capacity() { return N; }

    /* Store as much of src as fits; returns the bytes stored (0 when full). */
    size_t write(const void *src, size_t len)
    {
        typename Sharing::Guard g;
        const size_t n = len < _b.space() ? len : _b.space();
        _b.put(static_cast<const uint8_t *>(src), n);
        return n;
    }

    bool write(uint8_t byte) { return write(&byte, 1) == 1; }

    /* Take up to len bytes; returns how many. */
    size_t read(void *dst, size_t len)
    {
        typename Sharing::Guard g;
        const size_t n = len < _b.used ? len : _b.used;
        _b.peek(static_cast<uint8_t *>(dst), n);
        _b.drop(n);
        return n;
    }

    /* The next byte, or -1 when empty (Stream's shape). */
    int read()
    {
        uint8_t b;
        return read(&b, 1) == 1 ? b : -1;
    }

    int peek() const
    {
        typename Sharing::Guard g;
        if (_b.used == 0) {
            return -1;
        }
        uint8_t b;
        _b.peek(&b, 1);
        return b;
    }

    size_t available() const
    {
        typename Sharing::Guard g;
        return _b.used;
    }

    size_t space() const
    {
        typename Sharing::Guard g;
        return _b.space();
    }

    void clear()
    {
        typename Sharing::Guard g;
        _b.head = 0;
        _b.used = 0;
    }

private:
    detail::Bytes<N> _b;
};

template <size_t N, typename Sharing = IsrSafe>
class MessageBuffer {
    static_assert(N > 2, "a message needs 2 bytes of length plus its data");

public:
    /* The longest message that fits in an empty buffer. */
    static constexpr size_t maxMessage() { return N - 2; }

    /* Store the whole message, or nothing (false: no room now, or longer than
     * maxMessage() ever allows). An empty message is a message. */
    bool trySend(const void *src, size_t len)
    {
        if (len > maxMessage()) {
            return false;
        }
        typename Sharing::Guard g;
        if (_b.space() < len + 2) {
            return false;
        }
        const uint8_t n[2] = {(uint8_t)len, (uint8_t)(len >> 8)};
        _b.put(n, 2);
        _b.put(static_cast<const uint8_t *>(src), len);
        _messages++;
        return true;
    }

    /* The length of the next message; 0 also for an empty message, so check
     * count() to tell "none" from "empty". */
    size_t nextLength() const
    {
        typename Sharing::Guard g;
        return _messages ? length() : 0;
    }

    /* Take one whole message into dst (room for max bytes); returns its
     * length. When there is none, or it does not fit, returns 0 and leaves it
     * in the buffer - check count() / nextLength(). */
    size_t tryReceive(void *dst, size_t max)
    {
        typename Sharing::Guard g;
        if (_messages == 0) {
            return 0;
        }
        const size_t len = length();
        if (len > max) {
            return 0;
        }
        _b.peek(static_cast<uint8_t *>(dst), len, 2);
        _b.drop(len + 2);
        _messages--;
        return len;
    }

    size_t count() const
    {
        typename Sharing::Guard g;
        return _messages;
    }

    void clear()
    {
        typename Sharing::Guard g;
        _b.head = 0;
        _b.used = 0;
        _messages = 0;
    }

private:
    size_t length() const
    {
        uint8_t n[2];
        _b.peek(n, 2);
        return (size_t)n[0] | ((size_t)n[1] << 8);
    }

    detail::Bytes<N> _b;
    uint16_t _messages = 0;
};

}  // namespace CH32RVSyncKit
