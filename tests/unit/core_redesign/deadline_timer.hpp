#pragma once
#include <stdint.h>

namespace experiment {
// TIME-01/02 serial-context experiment. update() is explicit; no clock source,
// tick, callback or executor dependency. Comparisons must obey the half-range
// time contract, including the maximum interval between relevant observations.
class DeadlineTimer {
public:
    struct Expiration { uint32_t latest_due = 0, skipped = 0; bool saturated = false; };
    static constexpr uint32_t half = 0x80000000u;
private:
    uint32_t next = 0, interval = 0;
    Expiration expiry;
    bool active = false, pending = false;
public:
    DeadlineTimer() = default;
    bool armed() const { return active; }
    bool ready() const { return pending; }
    uint32_t deadline() const { return next; }
    uint32_t period() const { return interval; }
    // Accepted replacement explicitly discards the old schedule/notification.
    // Invalid input preserves both. period==0 denotes a one-shot.
    bool arm(uint32_t now, uint32_t delay, uint32_t period = 0) {
        if (delay >= half || period >= half) return false;
        next = now + delay; interval = period; active = true; pending = false; expiry = {};
        return true;
    }
    // Stop future expirations; an already published expiration stays sticky.
    bool stop() { const bool was_active = active; active = false; return was_active; }
    bool update(uint32_t now) {
        const uint32_t late = now - next;
        if (!active || late >= half) return false;
        const uint32_t extra = interval ? late / interval : 0;
        expiry.latest_due = next + extra * interval;
        const uint32_t add = extra + (pending ? 1u : 0u);
        if (!pending) { expiry.skipped = 0; expiry.saturated = false; }
        if (expiry.skipped > UINT32_MAX - add) {
            expiry.skipped = UINT32_MAX; expiry.saturated = true;
        } else expiry.skipped += add;
        pending = true;
        if (interval) next = expiry.latest_due + interval;
        else active = false;
        return true;
    }
    bool take(Expiration& value) {
        if (!pending) return false;
        value = expiry; pending = false;
        return true;
    }
};
} // namespace experiment
