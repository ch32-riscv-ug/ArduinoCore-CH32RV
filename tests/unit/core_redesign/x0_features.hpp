#pragma once
#include "x0_candidates.hpp"

namespace experiment {
// New compile-time feature comparison. Historical runtime-flag X0 is unchanged.
template<bool Enabled> struct TouchStorage {};
template<> struct TouchStorage<true> {
    uint32_t next_touch = 0, touch_done = 0, missed_touch = 0;
    uint8_t touch_data[8]{};
    bool touch_pending = false;
};
template<unsigned Owners> struct FeatureBus {
    uint8_t owner = 0, length = 0;
    const uint8_t* borrowed = nullptr;
    uint32_t now = 0, due = 0, sealed_hash = 0;
    bool active = false, terminal = false, success = false, fail_next[Owners + 1]{};
    uint32_t started = 0, completed = 0, reclaimed = 0, busy = 0, corruptions = 0;
    uint32_t owner_starts[Owners + 1]{};

    bool try_start(uint8_t target, const uint8_t* data, uint8_t size) {
        if (!target || target > Owners) return false;
        if (active) { ++busy; return false; }
        owner = target; borrowed = data; length = size;
        sealed_hash = p2::bytes_hash(data, size);
        active = true; terminal = false; success = !fail_next[target];
        fail_next[target] = false; due = now + 2;
        ++started; ++owner_starts[target];
        return true;
    }
    void hardware(uint32_t time) {
        now = time;
        if (active && p2::bytes_hash(borrowed, length) != sealed_hash) ++corruptions;
        if (active && !terminal && now >= due) { terminal = true; ++completed; }
    }
    bool take(uint8_t target, bool& ok) {
        if (!active || !terminal || owner != target) return false;
        ok = success; active = false; terminal = false; owner = 0;
        borrowed = nullptr; ++reclaimed;
        return true;
    }
};

template<class Inbox, bool Touch> struct FixedX0Meaning : TouchStorage<Touch> {
    X0Pps<Inbox> pps;
    p2::Screen screen;
    FeatureBus<Touch ? 3 : 2> bus;
    uint32_t now = 0, next_measure = 0;
    uint32_t measurement_done = 0, missed_measure = 0;
    uint8_t last_granted = 0;
    bool measure_pending = false;
    int32_t source_mv = 5000, source_ma = 1000;
    uint8_t measurement_data[8]{};

    static constexpr bool touch_feature = Touch;
    uint32_t touch_count() const { if constexpr (Touch) return this->touch_done; else return 0; }
    uint32_t touch_missed() const { if constexpr (Touch) return this->missed_touch; else return 0; }
    uint32_t touch_starts() const { if constexpr (Touch) return bus.owner_starts[3]; else return 0; }
    bool touch_due() const { if constexpr (Touch) return now >= this->next_touch; else return false; }
    bool touch_waiting() const { if constexpr (Touch) return this->touch_pending; else return false; }

    static void encode(uint8_t* out, int32_t value) {
        for (unsigned i = 0; i < 4; ++i) out[i] = uint8_t(uint32_t(value) >> (8 * i));
    }
    static int32_t decode(const uint8_t* in) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t(in[i]) << (8 * i);
        return value <= 0x7fffffffu ? int32_t(value) : int32_t(-1 - int32_t(~value));
    }
    static bool same_view(const p2::View& a, const p2::View& b) {
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
    bool needs_step() const {
        const p2::View view{pps.desired, pps.confirmed, pps.measured_mv, pps.measured_ma,
                            pps.valid, pps.stale, pps.connected};
        if (pps.inbox.size() || (pps.active && pps.terminal) ||
            (!pps.active && pps.selected_pending && pps.connected) || bus.terminal ||
            !same_view(view, screen.latest) || now >= next_measure ||
            touch_due() || (screen.active && screen.terminal)) return true;
        if (!screen.frame && screen.revision != screen.displayed_revision && now >= screen.next_frame) return true;
        return !bus.active && ((screen.frame && !screen.active) || measure_pending || touch_waiting());
    }
    void step() {
        bool ok;
        if (bus.take(1, ok)) screen.finish(ok);
        if (bus.take(2, ok)) {
            ++measurement_done;
            pps.measurement(ok, decode(measurement_data), decode(measurement_data + 4));
        }
        if constexpr (Touch) { if (bus.take(3, ok) && ok) ++this->touch_done; }
        pps.step();
        const p2::View view{pps.desired, pps.confirmed, pps.measured_mv, pps.measured_ma,
                        pps.valid, pps.stale, pps.connected};
        if (!same_view(view, screen.latest)) screen.update(view);
        periodic(now, 100, next_measure, measure_pending, missed_measure);
        if constexpr (Touch) periodic(now, 20, this->next_touch, this->touch_pending, this->missed_touch);
        screen.now = now;
        screen.step(false);
        if (bus.active) return;
        // Every successful transaction releases the bus before this selection.
        // No request is enqueued; readiness remains in its owning state machine.
        constexpr unsigned owners = Touch ? 3 : 2;
        for (unsigned i = 1; i <= owners; ++i) {
            const uint8_t owner = uint8_t((last_granted + i - 1) % owners + 1);
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
            if constexpr (Touch) {
                if (owner == 3 && this->touch_pending) {
                    encode(this->touch_data, int32_t(this->touch_done + 1)); encode(this->touch_data + 4, 0);
                    if (bus.try_start(3, this->touch_data, 8)) { this->touch_pending = false; last_granted = 3; }
                    return;
                }
            }
        }
    }
};

// Independently stripped no-touch control: no feature storage base or hooks.
template<class Inbox> struct ManualX0Meaning {
    X0Pps<Inbox> pps;
    p2::Screen screen;
    FeatureBus<2> bus;
    uint32_t now = 0, next_measure = 0;
    uint32_t measurement_done = 0, missed_measure = 0;
    uint8_t last_granted = 0;
    bool measure_pending = false;
    int32_t source_mv = 5000, source_ma = 1000;
    uint8_t measurement_data[8]{};

    static constexpr bool touch_feature = false;
    uint32_t touch_count() const { return 0; }
    uint32_t touch_missed() const { return 0; }
    uint32_t touch_starts() const { return 0; }

    static void encode(uint8_t* out, int32_t value) {
        for (unsigned i = 0; i < 4; ++i) out[i] = uint8_t(uint32_t(value) >> (8 * i));
    }
    static int32_t decode(const uint8_t* in) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t(in[i]) << (8 * i);
        return value <= 0x7fffffffu ? int32_t(value) : int32_t(-1 - int32_t(~value));
    }
    static bool same_view(const p2::View& a, const p2::View& b) {
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
    bool needs_step() const {
        const p2::View view{pps.desired, pps.confirmed, pps.measured_mv, pps.measured_ma,
                            pps.valid, pps.stale, pps.connected};
        if (pps.inbox.size() || (pps.active && pps.terminal) ||
            (!pps.active && pps.selected_pending && pps.connected) || bus.terminal ||
            !same_view(view, screen.latest) || now >= next_measure ||
            (screen.active && screen.terminal)) return true;
        if (!screen.frame && screen.revision != screen.displayed_revision && now >= screen.next_frame) return true;
        return !bus.active && ((screen.frame && !screen.active) || measure_pending);
    }
    void step() {
        bool ok;
        if (bus.take(1, ok)) screen.finish(ok);
        if (bus.take(2, ok)) {
            ++measurement_done;
            pps.measurement(ok, decode(measurement_data), decode(measurement_data + 4));
        }
        pps.step();
        const p2::View view{pps.desired, pps.confirmed, pps.measured_mv, pps.measured_ma,
                        pps.valid, pps.stale, pps.connected};
        if (!same_view(view, screen.latest)) screen.update(view);
        periodic(now, 100, next_measure, measure_pending, missed_measure);
        screen.now = now;
        screen.step(false);
        if (bus.active) return;
        // Every successful transaction releases the bus before this selection.
        // No request is enqueued; readiness remains in its owning state machine.
        constexpr unsigned owners = 2;
        for (unsigned i = 1; i <= owners; ++i) {
            const uint8_t owner = uint8_t((last_granted + i - 1) % owners + 1);
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
        }
    }
};

template<unsigned Candidate> struct FeatureInbox { using Type = Queue<X0Envelope, 4>; };
template<> struct FeatureInbox<1> { using Type = X0DirectInbox; };
template<> struct FeatureInbox<4> { using Type = X0BatchInbox; };

template<unsigned Candidate, class Meaning> struct FeatureAdapter : Meaning {
    bool initialized = false, opted = false;
    WorkDomain domain = WorkDomain::System;
    void begin(WorkDomain d = WorkDomain::System, bool opt = false) { domain = d; opted = opt; initialized = true; }
    void changed() {}
    TurnStatus opportunity(WorkOpportunity where, WorkProfile profile, WorkContext context = WorkContext::Normal) {
        if (!initialized) return TurnStatus::NotInitialized;
        if (context == WorkContext::Setup || context == WorkContext::Isr) return TurnStatus::Forbidden;
        if (context == WorkContext::Callback) return TurnStatus::Reentrant;
        if (where != WorkOpportunity::Explicit) {
            if (profile == WorkProfile::M) return TurnStatus::Suppressed;
            if (domain == WorkDomain::User && (profile != WorkProfile::U || !opted)) return TurnStatus::Ran;
        }
        this->step(); return TurnStatus::Ran;
    }
};
template<class Meaning> struct FeatureAdapter<3, Meaning> : Meaning {
    WorkExecutor<1> executor;
    WorkExecutor<1>::Token target = WorkExecutor<1>::invalid();
    static void handler(void* context, WorkExecutor<1>::Token) {
        auto& self = *static_cast<FeatureAdapter*>(context); self.step(); self.changed();
    }
    void begin(WorkDomain domain = WorkDomain::System, bool opt = false) {
        target = executor.add(handler, this, domain, opt); executor.initialize(); changed();
    }
    void changed() { if (this->needs_step()) executor.post(target); }
    TurnStatus opportunity(WorkOpportunity where, WorkProfile profile, WorkContext context = WorkContext::Normal) {
        return executor.turn(8, where, profile, context).status;
    }
};
template<class Meaning> struct FeatureAdapter<4, Meaning> : Meaning {
    void begin(WorkDomain domain = WorkDomain::System, bool opt = false) {
        this->pps.inbox.mailbox.begin(domain, opt); changed();
    }
    void changed() { if (this->needs_step()) this->pps.inbox.mailbox.notify(1); }
    TurnStatus opportunity(WorkOpportunity where, WorkProfile profile, WorkContext context = WorkContext::Normal) {
        return this->pps.inbox.mailbox.deliver(where, profile, context, [&](const X0BatchInbox::Mailbox::Batch& batch) {
            this->pps.inbox.current = &batch; this->pps.inbox.cursor = 0;
            this->step(); this->pps.inbox.current = nullptr; changed();
        });
    }
};
template<unsigned Candidate, bool Touch>
using FixedX0 = FeatureAdapter<Candidate, FixedX0Meaning<typename FeatureInbox<Candidate>::Type, Touch>>;
template<unsigned Candidate>
using ManualX0 = FeatureAdapter<Candidate, ManualX0Meaning<typename FeatureInbox<Candidate>::Type>>;
} // namespace experiment
