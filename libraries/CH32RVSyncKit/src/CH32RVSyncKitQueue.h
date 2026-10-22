/* Queue<T, N>: a fixed-size FIFO of values, the way an ISR hands data to
 * loop(). Same names as ESP32SyncKit's Queue, without the waiting forms.
 *
 *     CH32RVSyncKit::Queue<uint32_t, 8> times;  // 8 values, ISR-safe
 *     void onPin() { times.trySend(millis()); } // ISR: hand it over, return
 *     void loop() { uint32_t t; while (times.tryReceive(t)) print(t); }
 *
 * Values are copied in and out (T must be trivially copyable: numbers, small
 * structs, pointers). A full queue refuses the new value and keeps the old
 * ones - nothing is overwritten unless the code says overwrite(). */
#pragma once

#include <string.h>
#include <type_traits>

#include "CH32RVSyncKitCommon.h"

namespace CH32RVSyncKit {

template <typename T, size_t N, typename Sharing = IsrSafe>
class Queue {
    static_assert(N > 0 && N <= 65535u, "Queue holds 1 to 65535 values");
    static_assert(std::is_trivially_copyable<T>::value,
                  "Queue copies values in and out: use numbers, small structs or pointers");

public:
    static constexpr size_t capacity() { return N; }

    /* Add at the back. False when full; the queue is unchanged. */
    bool trySend(const T &value)
    {
        typename Sharing::Guard g;
        if (_count == N) {
            return false;
        }
        _slot[wrap(_head + _count)] = value;
        _count++;
        return true;
    }

    /* Add at the front (the next tryReceive() returns it). Breaks FIFO order;
     * for an urgent value. False when full. */
    bool trySendToFront(const T &value)
    {
        typename Sharing::Guard g;
        if (_count == N) {
            return false;
        }
        _head = (uint16_t)(_head == 0 ? N - 1 : _head - 1);
        _slot[_head] = value;
        _count++;
        return true;
    }

    /* Replace the one value of a one-place queue (a mailbox that always holds
     * the latest). Only for Queue<T, 1>: on a longer queue it would drop
     * values silently. */
    bool overwrite(const T &value)
    {
        static_assert(N == 1, "overwrite() is for a one-place mailbox, Queue<T, 1>");
        typename Sharing::Guard g;
        _slot[0] = value;
        _head = 0;
        _count = 1;
        return true;
    }

    /* Take the front value. False when empty; out is unchanged. */
    bool tryReceive(T &out)
    {
        typename Sharing::Guard g;
        if (_count == 0) {
            return false;
        }
        out = _slot[_head];
        _head = (uint16_t)wrap(_head + 1u);
        _count--;
        return true;
    }

    /* Copy the front value without taking it. */
    bool tryPeek(T &out) const
    {
        typename Sharing::Guard g;
        if (_count == 0) {
            return false;
        }
        out = _slot[_head];
        return true;
    }

    size_t count() const
    {
        typename Sharing::Guard g;
        return _count;
    }
    size_t spaces() const { return N - count(); }
    bool isEmpty() const { return count() == 0; }
    bool isFull() const { return count() == N; }

    void clear()
    {
        typename Sharing::Guard g;
        _head = 0;
        _count = 0;
    }

private:
    static size_t wrap(size_t i) { return i >= N ? i - N : i; }

    T _slot[N];
    uint16_t _head = 0;
    uint16_t _count = 0;
};

}  // namespace CH32RVSyncKit
