/* BlockPool<T, N> and TryLock: lending things out.
 *
 *   BlockPool  N blocks of T, lent one at a time. tryAllocate() gives a block
 *              or nullptr when all are out; release() takes it back and
 *              refuses a pointer that is not one of its blocks or is not out
 *              (a double release). Pair it with a Queue<T *, N> to hand big
 *              records from an ISR to loop() without copying them.
 *   TryLock    "this resource is mine for a moment". tryLock() succeeds or
 *              says it is taken; nobody waits. An ISR that finds it taken
 *              skips its work (and counts that) instead of waiting for loop()
 *              - which it never could, loop() cannot run until it returns.
 *              Never hold it across a delay() or from one loop() to the next
 *              unless that is the point (a buffer being filled in steps). */
#pragma once

#include <type_traits>

#include "CH32RVSyncKitCommon.h"

namespace CH32RVSyncKit {

template <typename T, size_t N, typename Sharing = IsrSafe>
class BlockPool {
    static_assert(N > 0 && N <= 255u, "BlockPool holds 1 to 255 blocks");
    static_assert(std::is_trivially_destructible<T>::value,
                  "BlockPool lends raw blocks; use a type without a destructor");

public:
    static constexpr size_t capacity() { return N; }

    T *tryAllocate()
    {
        typename Sharing::Guard g;
        for (size_t w = 0; w < WORDS; w++) {
            const uint32_t free = ~_out[w] & mask(w);
            if (free) {
                const unsigned bit = (unsigned)__builtin_ctz(free);
                _out[w] |= 1u << bit;
                return &_block[w * 32u + bit];
            }
        }
        return nullptr;
    }

    /* False for a pointer that is not one of this pool's blocks, or one that
     * is not out. */
    bool release(T *block)
    {
        if (block < _block || block >= _block + N) {
            return false;
        }
        const size_t i = (size_t)(block - _block);
        typename Sharing::Guard g;
        const uint32_t bit = 1u << (i % 32u);
        if (!(_out[i / 32u] & bit)) {
            return false;
        }
        _out[i / 32u] &= ~bit;
        return true;
    }

    size_t available() const
    {
        typename Sharing::Guard g;
        size_t n = 0;
        for (size_t w = 0; w < WORDS; w++) {
            n += (size_t)__builtin_popcount(~_out[w] & mask(w));
        }
        return n;
    }

private:
    static constexpr size_t WORDS = (N + 31u) / 32u;
    static constexpr uint32_t mask(size_t w)
    {
        return (w + 1u) * 32u <= N ? 0xFFFFFFFFu : (1u << (N % 32u)) - 1u;
    }

    T _block[N];
    uint32_t _out[WORDS] = {};
};

template <typename Sharing = IsrSafe>
class TryLock {
public:
    bool tryLock()
    {
        typename Sharing::Guard g;
        if (_locked) {
            return false;
        }
        _locked = true;
        return true;
    }

    void unlock()
    {
        typename Sharing::Guard g;
        _locked = false;
    }

    bool locked() const
    {
        typename Sharing::Guard g;
        return _locked;
    }

    /* tryLock() for a scope: unlocks when it goes out of scope, only if it
     * got the lock. Check locked() before using what it protects. */
    class Guard {
    public:
        explicit Guard(TryLock &lock) : _lock(lock), _held(lock.tryLock()) {}
        ~Guard()
        {
            if (_held) {
                _lock.unlock();
            }
        }
        bool locked() const { return _held; }
        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;

    private:
        TryLock &_lock;
        bool _held;
    };

private:
    bool _locked = false;
};

}  // namespace CH32RVSyncKit
