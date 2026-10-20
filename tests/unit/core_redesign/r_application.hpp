#pragma once
#include "r_workloads.hpp"

namespace p2 {

inline uint32_t bytes_hash(const uint8_t* data, unsigned size) {
    uint32_t value = 2166136261u;
    for (unsigned i = 0; i < size; ++i) value = (value ^ data[i]) * 16777619u;
    return value;
}

// R-specific simulated bus: one accepted request and a sticky terminal slot.
// Payload is borrowed through terminal collection, even if the app stops.
struct SharedBus {
    uint8_t owner = 0, length = 0;
    const uint8_t* borrowed = nullptr;
    uint32_t now = 0, due = 0, sealed_hash = 0;
    bool active = false, terminal = false, success = false, fail_next[4]{};
    uint32_t started = 0, completed = 0, reclaimed = 0, busy = 0, corruptions = 0;
    uint32_t owner_starts[4]{};

    bool try_start(uint8_t target, const uint8_t* data, uint8_t size) {
        if (active) { ++busy; return false; }
        owner = target; borrowed = data; length = size;
        sealed_hash = bytes_hash(data, size);
        active = true; terminal = false; success = !fail_next[target];
        fail_next[target] = false; due = now + 2;
        ++started; ++owner_starts[target];
        return true;
    }
    void hardware(uint32_t time) {
        now = time;
        if (active && bytes_hash(borrowed, length) != sealed_hash) ++corruptions;
        if (active && !terminal && now >= due) { terminal = true; ++completed; }
    }
    bool take(uint8_t target, bool& ok) {
        if (!active || !terminal || owner != target) return false;
        ok = success; active = false; terminal = false; owner = 0;
        borrowed = nullptr; ++reclaimed;
        return true;
    }
};

struct Application {
    Pps pps;
    Screen screen;
    SharedBus bus;
    uint32_t now = 0, next_measure = 0, next_touch = 0;
    uint32_t measurement_done = 0, touch_done = 0, missed_measure = 0, missed_touch = 0;
    uint8_t last_granted = 0;
    bool touch_enabled = false, measure_pending = false, touch_pending = false;
    int32_t source_mv = 5000, source_ma = 1000;
    uint8_t measurement_data[8]{}, touch_data[8]{};

    static void encode(uint8_t* out, int32_t value) {
        for (unsigned i = 0; i < 4; ++i) out[i] = uint8_t(uint32_t(value) >> (8 * i));
    }
    static int32_t decode(const uint8_t* in) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t(in[i]) << (8 * i);
        return value <= 0x7fffffffu ? int32_t(value) : int32_t(-1 - int32_t(~value));
    }
    static bool same_view(const View& a, const View& b) {
        return a.desired == b.desired && a.confirmed == b.confirmed && a.mv == b.mv && a.ma == b.ma &&
               a.valid == b.valid && a.stale == b.stale && a.connected == b.connected;
    }
    static void periodic(uint32_t now, uint32_t period, uint32_t& next, bool& pending, uint32_t& missed) {
        if (now < next) return;
        const uint32_t count = (now - next) / period + 1;
        missed += count - 1 + (pending ? 1 : 0);
        pending = true; next += count * period;
    }
    void hardware(uint32_t time) {
        now = time; pps.hardware(time); bus.hardware(time);
    }
    void step() {
        bool ok;
        if (bus.take(1, ok)) screen.finish(ok);
        if (bus.take(2, ok)) {
            ++measurement_done;
            pps.measurement(ok, decode(measurement_data), decode(measurement_data + 4));
        }
        if (bus.take(3, ok)) { if (ok) ++touch_done; }
        pps.step();
        const View view{pps.desired, pps.confirmed, pps.measured_mv, pps.measured_ma,
                        pps.valid, pps.stale, pps.connected};
        if (!same_view(view, screen.latest)) screen.update(view);
        periodic(now, 100, next_measure, measure_pending, missed_measure);
        if (touch_enabled) periodic(now, 50, next_touch, touch_pending, missed_touch);
        screen.now = now;
        screen.step(false);
        if (bus.active) return;
        // Every successful transaction releases the bus before this selection.
        // No request is enqueued; readiness remains in its owning state machine.
        for (unsigned i = 1; i <= 3; ++i) {
            const uint8_t owner = uint8_t((last_granted + i - 1) % 3 + 1);
            if (owner == 1 && screen.frame && !screen.active) {
                screen.prepare_chunk();
                if (bus.try_start(1, screen.tx, 32)) { screen.mark_started(); last_granted = 1; }
                return;
            }
            if (owner == 2 && measure_pending) {
                encode(measurement_data, source_mv); encode(measurement_data + 4, source_ma);
                if (bus.try_start(2, measurement_data, 8)) { measure_pending = false; last_granted = 2; }
                return;
            }
            if (owner == 3 && touch_enabled && touch_pending) {
                encode(touch_data, int32_t(touch_done + 1)); encode(touch_data + 4, 0);
                if (bus.try_start(3, touch_data, 8)) { touch_pending = false; last_granted = 3; }
                return;
            }
        }
    }
};

} // namespace p2
