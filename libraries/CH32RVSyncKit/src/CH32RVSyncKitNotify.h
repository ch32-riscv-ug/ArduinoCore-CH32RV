/* Notify and BinarySemaphore: "something happened" without the data. Same
 * names as ESP32SyncKit's, without the waiting forms.
 *
 * Notify has a counter and a set of 32 bits; use one or the other per object.
 *
 *   counter  notify() adds one (an ISR counts events), tryTake() takes one,
 *            tryTakeAll() takes them all and says how many - a light counting
 *            semaphore. The counter stops at its maximum instead of wrapping.
 *   bits     setBits(mask) raises bits (an ISR says which events), and
 *            tryWaitBits(mask) looks for them: any of the mask, or all of it
 *            with waitAll, and clears what it found unless clearOnExit is
 *            false. Raising a bit twice before it is taken is one event.
 *
 * BinarySemaphore is the single "happened / not yet": give() sets it, tryTake()
 * takes it. A second give() before the take is refused (returns false). */
#pragma once

#include "CH32RVSyncKitCommon.h"

namespace CH32RVSyncKit {

template <typename Sharing = IsrSafe>
class Notify {
public:
    /* Count one event. False only when the counter is already at its
     * maximum (the event is not counted). */
    bool notify()
    {
        typename Sharing::Guard g;
        if (_count == UINT32_MAX) {
            return false;
        }
        _count++;
        return true;
    }

    bool tryTake()
    {
        typename Sharing::Guard g;
        if (_count == 0) {
            return false;
        }
        _count--;
        return true;
    }

    uint32_t tryTakeAll()
    {
        typename Sharing::Guard g;
        const uint32_t n = _count;
        _count = 0;
        return n;
    }

    uint32_t pending() const
    {
        typename Sharing::Guard g;
        return _count;
    }

    void setBits(uint32_t mask)
    {
        typename Sharing::Guard g;
        _bits |= mask;
    }

    void clearBits(uint32_t mask)
    {
        typename Sharing::Guard g;
        _bits &= ~mask;
    }

    /* The bits of mask that are up (any of them, or all of them with
     * waitAll); 0 when the condition is not met. With clearOnExit the bits
     * returned are cleared in the same step, so an ISR raising one again in
     * between is not lost. */
    uint32_t tryWaitBits(uint32_t mask, bool clearOnExit = true, bool waitAll = false)
    {
        typename Sharing::Guard g;
        const uint32_t up = _bits & mask;
        if (up == 0 || (waitAll && up != mask)) {
            return 0;
        }
        if (clearOnExit) {
            _bits &= ~up;
        }
        return up;
    }

    uint32_t bits() const
    {
        typename Sharing::Guard g;
        return _bits;
    }

private:
    uint32_t _count = 0;
    uint32_t _bits = 0;
};

template <typename Sharing = IsrSafe>
class BinarySemaphore {
public:
    bool give()
    {
        typename Sharing::Guard g;
        if (_given) {
            return false;
        }
        _given = true;
        return true;
    }

    bool tryTake()
    {
        typename Sharing::Guard g;
        if (!_given) {
            return false;
        }
        _given = false;
        return true;
    }

    bool given() const
    {
        typename Sharing::Guard g;
        return _given;
    }

private:
    bool _given = false;
};

}  // namespace CH32RVSyncKit
