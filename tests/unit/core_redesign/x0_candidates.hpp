#pragma once
#include "r_application.hpp"
#include "static_mailbox.hpp"

namespace experiment {
struct X0Envelope { p2::Pps::Command command{}; uint32_t session = 0; };
static_assert(sizeof(X0Envelope) == 8, "4-byte input plus 4-byte session metadata");
struct X0DirectInbox {
    X0Envelope messages[4]{};
    uint8_t head = 0, count = 0;
    unsigned size() const { return count; }
    bool push(const X0Envelope& value) {
        if (count == 4) return false;
        messages[(head + count) % 4] = value; ++count; return true;
    }
    bool pop(X0Envelope& value) {
        if (!count) return false;
        value = messages[head]; head = uint8_t((head + 1) % 4); --count; return true;
    }
};
struct X0BatchInbox {
    using Mailbox = StaticMailbox<X0Envelope, 4>;
    Mailbox mailbox;
    const Mailbox::Batch* current = nullptr;
    unsigned cursor = 0;
    unsigned size() const { return current ? current->count - cursor : mailbox.size(); }
    bool push(const X0Envelope& value) { return mailbox.send(value) == EventSend::Accepted; }
    bool pop(X0Envelope& value) {
        if (!current || cursor == current->count) return false;
        value = current->messages[cursor++]; return true;
    }
};

template<class Inbox> struct X0Pps {
    struct Command { int16_t kind; int16_t value; };
    static_assert(sizeof(Command) == 4, "X0 input payload must be four bytes");
    Inbox inbox;
    uint32_t session = 0, active_session = 0, request_id = 0;
    uint32_t accepted_count = 0, rejected_count = 0, started = 0, reclaimed = 0;
    int32_t desired = 5000, selected = 0, sent = 0, confirmed = 0;
    uint32_t now = 0, due = 0;
    bool connected = false, active = false, terminal = false, terminal_success = false;
    bool valid = false, selected_pending = false;
    int32_t measured_mv = 0, measured_ma = 0;
    bool stale = true;

    void attach() {
        ++session;
        connected = true; valid = false; selected_pending = false;
    }
    void detach() {
        connected = false; valid = false; selected_pending = false;
        if (active && !terminal) { terminal = true; terminal_success = false; }
    }
    bool enqueue(int16_t kind) {
        if (!inbox.push({{kind, 0}, connected ? session : 0})) { ++rejected_count; return false; }
        ++accepted_count;
        return true;
    }
    void completion(uint32_t id, uint32_t connection, bool success) {
        if (!active || terminal || id != request_id || connection != active_session) return;
        terminal = true; terminal_success = success;
    }
    void hardware(uint32_t time) {
        now = time;
        if (active && !terminal && now >= due) completion(request_id, active_session, true);
    }
    void measurement(bool ok, int32_t mv, int32_t ma) {
        if (ok) { measured_mv = mv; measured_ma = ma; }
        stale = !ok;
    }
    void step() {
        // Snapshot the number of input commands; no producer is invoked here.
        const unsigned ready = inbox.size();
        for (unsigned i = 0; i < ready; ++i) {
            X0Envelope envelope;
            if (!inbox.pop(envelope)) break;
            const int kind = envelope.command.kind;
            const uint32_t input_session = envelope.session;
            if (kind == 1 && desired < 11000) desired += 20;
            if (kind == -1 && desired > 5000) desired -= 20;
            if (kind == 0 && connected && input_session == session) {
                selected = desired; selected_pending = true;
            }
        }
        if (active && terminal) {
            if (terminal_success && connected && session == active_session) {
                confirmed = sent; valid = true;
            }
            active = false; terminal = false; ++reclaimed;
        }
        if (!active && selected_pending && connected) {
            sent = selected; selected_pending = false;
            active = true; active_session = session; ++request_id; ++started;
            due = now + 5;
        }
    }
};

template<class Inbox> struct X0Meaning {
    X0Pps<Inbox> pps;
    p2::Screen screen;
    p2::SharedBus bus;
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
            (touch_enabled && now >= next_touch) || (screen.active && screen.terminal)) return true;
        if (!screen.frame && screen.revision != screen.displayed_revision && now >= screen.next_frame) return true;
        return !bus.active && ((screen.frame && !screen.active) || measure_pending || (touch_enabled && touch_pending));
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
        const p2::View view{pps.desired, pps.confirmed, pps.measured_mv, pps.measured_ma,
                        pps.valid, pps.stale, pps.connected};
        if (!same_view(view, screen.latest)) screen.update(view);
        periodic(now, 100, next_measure, measure_pending, missed_measure);
        if (touch_enabled) periodic(now, 20, next_touch, touch_pending, missed_touch);
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

template<class Inbox> struct X0Explicit : X0Meaning<Inbox> {
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
using X0R = X0Explicit<X0DirectInbox>;
using X0A = X0Explicit<Queue<X0Envelope, 4>>;
struct X0B : X0Meaning<Queue<X0Envelope, 4>> {
    WorkExecutor<1> executor;
    WorkExecutor<1>::Token target = WorkExecutor<1>::invalid();
    static void handler(void* context, WorkExecutor<1>::Token) {
        auto& self = *static_cast<X0B*>(context); self.step(); self.changed();
    }
    void begin(WorkDomain domain = WorkDomain::System, bool opt = false) {
        target = executor.add(handler, this, domain, opt); executor.initialize(); changed();
    }
    void changed() { if (needs_step()) executor.post(target); }
    TurnStatus opportunity(WorkOpportunity where, WorkProfile profile, WorkContext context = WorkContext::Normal) {
        return executor.turn(8, where, profile, context).status;
    }
};
// C-static-batch is an additional binding condition: one fixed application
// destination, no dynamic registration; <=4 accepted inputs per delivery.
struct X0C : X0Meaning<X0BatchInbox> {
    void begin(WorkDomain domain = WorkDomain::System, bool opt = false) {
        pps.inbox.mailbox.begin(domain, opt); changed();
    }
    void changed() { if (needs_step()) pps.inbox.mailbox.notify(1); }
    TurnStatus opportunity(WorkOpportunity where, WorkProfile profile, WorkContext context = WorkContext::Normal) {
        return pps.inbox.mailbox.deliver(where, profile, context, [&](const X0BatchInbox::Mailbox::Batch& batch) {
            pps.inbox.current = &batch; pps.inbox.cursor = 0;
            step(); pps.inbox.current = nullptr; changed();
        });
    }
};

} // namespace experiment
