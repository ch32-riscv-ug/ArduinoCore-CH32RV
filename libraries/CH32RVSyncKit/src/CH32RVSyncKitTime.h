/* Deadline and Period: "has the time come?" without a timer, a callback or
 * delay(). Each is two or three words of state that loop() checks; nothing
 * runs by itself, so nothing runs at a moment the sketch did not choose.
 *
 *   Deadline  one moment: arm(ms) from now, expired() once it has passed.
 *             For a timeout ("give up if no answer in 500 ms") or a retry.
 *   Period    a repeating moment: due() is true once per elapsed period.
 *             If loop() was late by several periods, due() is true once and
 *             missed() says how many were skipped - it does not fire them all
 *             in a burst. The next one stays on the original grid.
 *
 * Times are milliseconds from millis() unless the caller passes its own now.
 * 32-bit and wrap-safe: a deadline may be up to 2^31 - 1 ms (24.8 days)
 * ahead. These are only as punctual as loop(): a step that does not return
 * for 50 ms makes every deadline 50 ms late. */
#pragma once

#include "CH32RVSyncKitCommon.h"

namespace CH32RVSyncKit {

class Deadline {
public:
    static constexpr uint32_t MAX_MS = 0x7FFFFFFFu;

    /* Due ms from now (clamped to MAX_MS). Re-arming moves it. */
    void arm(uint32_t ms) { arm(CH32RVSyncKit::now(), ms); }
    void arm(uint32_t now, uint32_t ms)
    {
        _at = now + (ms > MAX_MS ? MAX_MS : ms);
        _armed = true;
    }

    void disarm() { _armed = false; }
    bool armed() const { return _armed; }

    /* Armed and the moment has passed. Stays true until re-armed or disarmed. */
    bool expired() const { return expired(CH32RVSyncKit::now()); }
    bool expired(uint32_t now) const { return _armed && (int32_t)(now - _at) >= 0; }

    /* Milliseconds left; 0 once expired, or when not armed. */
    uint32_t remaining() const { return remaining(CH32RVSyncKit::now()); }
    uint32_t remaining(uint32_t now) const
    {
        const int32_t left = (int32_t)(_at - now);
        return _armed && left > 0 ? (uint32_t)left : 0u;
    }

private:
    uint32_t _at = 0;
    bool _armed = false;
};

class Period {
public:
    /* ms between due() moments, 1 to 2^31 - 1. The first is ms after start(). */
    explicit Period(uint32_t ms) : _ms(ms == 0 ? 1u : (ms > Deadline::MAX_MS ? Deadline::MAX_MS : ms)) {}

    /* Start (or restart) the grid: the first due() is one period from now. A
     * Period that was never started starts at its first due() call. */
    void start() { start(CH32RVSyncKit::now()); }
    void start(uint32_t now)
    {
        _next = now + _ms;
        _started = true;
    }

    /* True once per elapsed period. */
    bool due() { return due(CH32RVSyncKit::now()); }
    bool due(uint32_t now)
    {
        if (!_started) {
            start(now);
            return false;
        }
        const int32_t late = (int32_t)(now - _next);
        if (late < 0) {
            return false;
        }
        const uint32_t skipped = (uint32_t)late / _ms;
        _missed += skipped;
        _next += (skipped + 1u) * _ms;
        return true;
    }

    /* Periods that passed without a due() of their own, since the last
     * clearMissed(). */
    uint32_t missed() const { return _missed; }
    void clearMissed() { _missed = 0; }

    uint32_t period() const { return _ms; }

private:
    uint32_t _ms;
    uint32_t _next = 0;
    uint32_t _missed = 0;
    bool _started = false;
};

}  // namespace CH32RVSyncKit
