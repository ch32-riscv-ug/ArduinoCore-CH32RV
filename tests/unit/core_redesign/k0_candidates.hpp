#pragma once
#include "event_executor.hpp"

namespace experiment {
struct K0Frame { uint8_t bytes[8]{}; };
template<unsigned N> struct K0DirectInbox {
    static_assert(N == 1 || N == 2, "K0 comparison capacities");
    K0Frame frames[N]{};
    uint8_t head = 0, count = 0;
    unsigned size() const { return count; }
    bool push(const K0Frame& frame) {
        if (count == N) return false;
        frames[(head + count) % N] = frame; ++count; return true;
    }
    bool pop(K0Frame& frame) {
        if (!count) return false;
        frame = frames[head]; head = uint8_t((head + 1) % N); --count; return true;
    }
};
template<unsigned N> struct K0EventInbox {
    using E = EventExecutor<K0Frame, 1, N>;
    E executor;
    typename E::Token target = E::invalid();
    const K0Frame* current = nullptr;
    unsigned size() const { return executor.queued(target); }
    bool push(const K0Frame& frame) { return executor.send(target, frame) == EventSend::Accepted; }
    bool pop(K0Frame& frame) {
        if (!current) return false;
        frame = *current; current = nullptr; return true;
    }
};

// Same frontend and meaning for all variants; inbox/execution are varied.
// Serialized logical I2C stimuli only. Actual IRQ atomicity and wire ACK/NACK
// are deliberately not inferred from this common algorithm.
template<class Inbox> struct K0Meaning {
    Inbox inbox;
    int32_t position = 0;
    int16_t increment = 1, lower = -1000, upper = 1000, delta = 0;
    uint16_t sequence = 0;
    uint8_t status = 0, published[8]{}, held[8]{}, received = 0, read_index = 0;
    K0Frame staging;
    bool writing = false, reading = false;
    int reply = -1, accepted = -1;
    static uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
    static int16_t i16(const uint8_t* p) {
        const int32_t value = u16(p); return int16_t(value >= 32768 ? value - 65536 : value);
    }
    void input(int16_t value) {
        const int32_t total = int32_t(delta) + value;
        if (total > 32767) { delta = 32767; status |= 1; }
        else if (total < -32768) { delta = -32768; status |= 1; }
        else delta = int16_t(total);
    }
    void abort_write() { if (writing) status |= 4; writing = false; }
    void begin_write(unsigned capacity) {
        abort_write(); reading = false; accepted = inbox.size() == capacity ? 0 : 1;
        if (!accepted) status |= 2;
        writing = accepted != 0; received = 0;
    }
    void byte(uint8_t value) {
        if (!writing) return;
        if (received < 8) staging.bytes[received] = value;
        if (received < 9) ++received;
    }
    void stop() {
        if (writing) {
            if (received != 8) status |= 4;
            else if (!inbox.push(staging)) status |= 2;
        }
        writing = reading = false;
    }
    void restart() { abort_write(); reading = false; }
    void begin_read() {
        abort_write();
        for (unsigned i = 0; i < 8; ++i) held[i] = published[i];
        reading = true; read_index = 0;
    }
    void read() {
        reply = !reading || read_index >= 8 ? 0 : held[read_index];
        if (read_index < 8) ++read_index;
    }
    bool needs_step() const { return inbox.size() || delta || published[6] != status; }
    void step() {
        K0Frame frame;
        if (inbox.pop(frame)) {
            const int16_t step = i16(frame.bytes + 2), lo = i16(frame.bytes + 4), hi = i16(frame.bytes + 6);
            if (step < 1 || step > 127 || lo > hi) status |= 4;
            else { increment = step; lower = lo; upper = hi; sequence = u16(frame.bytes); }
        }
        position += int32_t(delta) * increment; delta = 0;
        if (position < lower) position = lower;
        if (position > upper) position = upper;
        const auto value = uint32_t(position);
        for (unsigned i = 0; i < 4; ++i) published[i] = uint8_t(value >> (8 * i));
        published[4] = uint8_t(sequence); published[5] = uint8_t(sequence >> 8);
        published[6] = status; published[7] = 0;
    }
};

// R/A share meaning but use direct storage/common Queue respectively. No
// executor; the frontend explicitly signals after a serialized stimulus.
template<class Inbox, unsigned N> struct K0Explicit : K0Meaning<Inbox> {
    bool initialized = false;
    WorkDomain domain = WorkDomain::System;
    bool opted_in = false;
    void begin(WorkDomain d = WorkDomain::System, bool opt = false) { domain = d; opted_in = opt; initialized = true; }
    void changed() {}
    void begin_write() { K0Meaning<Inbox>::begin_write(N); }
    TurnStatus opportunity(WorkOpportunity where, WorkProfile profile, WorkContext context = WorkContext::Normal) {
        if (!initialized) return TurnStatus::NotInitialized;
        if (context == WorkContext::Setup || context == WorkContext::Isr) return TurnStatus::Forbidden;
        if (context == WorkContext::Callback) return TurnStatus::Reentrant;
        if (where != WorkOpportunity::Explicit) {
            if (profile == WorkProfile::M) return TurnStatus::Suppressed;
            if (domain == WorkDomain::User && (profile != WorkProfile::U || !opted_in)) return TurnStatus::Ran;
        }
        this->step(); return TurnStatus::Ran;
    }
};
template<unsigned N> using K0R = K0Explicit<K0DirectInbox<N>, N>;
template<unsigned N> using K0A = K0Explicit<Queue<K0Frame, N>, N>;
template<unsigned N> struct K0B : K0Meaning<Queue<K0Frame, N>> {
    using Base = K0Meaning<Queue<K0Frame, N>>;
    using E = WorkExecutor<1>;
    E executor;
    typename E::Token target = E::invalid();
    static void handler(void* context, typename E::Token) {
        auto& self = *static_cast<K0B*>(context); self.step(); self.changed();
    }
    void begin(WorkDomain d = WorkDomain::System, bool opt = false) {
        target = executor.add(handler, this, d, opt); executor.initialize(); changed();
    }
    void changed() { if (this->needs_step()) executor.post(target); }
    void begin_write() { Base::begin_write(N); }
    TurnStatus opportunity(WorkOpportunity where, WorkProfile profile, WorkContext context = WorkContext::Normal) {
        return executor.turn(8, where, profile, context).status;
    }
};
template<unsigned N> struct K0C : K0Meaning<K0EventInbox<N>> {
    using Inbox = K0EventInbox<N>;
    using Base = K0Meaning<Inbox>;
    static void handler(void* context, const typename Inbox::E::Delivery& d) {
        auto& self = *static_cast<K0C*>(context);
        self.inbox.current = d.has_message ? &d.message : nullptr;
        self.step(); self.inbox.current = nullptr;
    }
    void begin(WorkDomain d = WorkDomain::System, bool opt = false) {
        auto& inbox = this->inbox;
        inbox.target = inbox.executor.add(handler, this, d, opt); inbox.executor.initialize(); changed();
    }
    void changed() { if (this->needs_step()) this->inbox.executor.notify(this->inbox.target, 1); }
    void begin_write() { Base::begin_write(N); }
    TurnStatus opportunity(WorkOpportunity where, WorkProfile profile, WorkContext context = WorkContext::Normal) {
        return this->inbox.executor.turn(8, where, profile, context).status;
    }
};
} // namespace experiment
