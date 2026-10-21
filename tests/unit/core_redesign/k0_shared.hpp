#pragma once
#include "k0_candidates.hpp"

namespace experiment {
// N03 single-core, maskable-IRQ exchange. IRQ methods never enter an executor.
// Queue, accumulated input and the two snapshots are protected; validation and
// the user-facing work/event handler execute outside the owned gate scopes.
template<class Inbox,class Gate> struct K0SharedMeaning {
    struct Scope {
        typename Gate::Token token{};
        Scope() { Gate::enter(token); }
        ~Scope() { Gate::leave(token); }
    };
    Inbox inbox;
    K0Frame staging;
    int16_t delta = 0;
    uint8_t status = 0, received = 0, read_index = 0, published[8]{}, held[8]{};
    bool writing = false, reading = false;
    int reply = -1, accepted = -1;
    int32_t position = 0;
    int16_t increment = 1, lower = -1000, upper = 1000;
    uint16_t sequence = 0;
    static void abort_write_body(K0SharedMeaning& self) {
        if (self.writing) self.status |= 4;
        self.writing = false;
    }
    void input(int16_t value) {
        Scope scope;
        const int32_t total = int32_t(delta)+value;
        if (total > 32767) { delta = 32767; status |= 1; }
        else if (total < -32768) { delta = -32768; status |= 1; }
        else delta = int16_t(total);
    }
    void begin_write(unsigned capacity) {
        Scope scope;
        abort_write_body(*this); reading = false;
        accepted = inbox.size() < capacity;
        if (!accepted) status |= 2;
        writing = accepted != 0; received = 0;
    }
    void byte(uint8_t value) {
        Scope scope;
        if (!writing) return;
        if (received < 8) staging.bytes[received] = value;
        if (received < 9) ++received;
    }
    void stop() {
        Scope scope;
        if (writing) {
            if (received != 8) status |= 4;
            else if (!inbox.push(staging)) status |= 2;
        }
        writing = reading = false;
    }
    void restart() { Scope scope; abort_write_body(*this); reading = false; }
    void begin_read() {
        Scope scope;
        abort_write_body(*this);
        for (unsigned i = 0; i < 8; ++i) held[i] = published[i];
        reading = true; read_index = 0;
    }
    void read() {
        Scope scope;
        reply = !reading || read_index >= 8 ? 0 : held[read_index];
        if (read_index < 8) ++read_index;
    }
    bool needs_step() {
        Scope scope;
        Gate::point(0,0);
        return inbox.size() || delta || published[6] != status;
    }
    void step() {
        Gate::normal();
        K0Frame frame;
        bool have;
        int16_t amount;
        {
            Scope scope;
            have = inbox.pop(frame);
            Gate::point(1,0);
            amount = delta;
            Gate::point(1,1); // Unsafe negative control can lose an IRQ here.
            delta = 0;
            Gate::point(1,2);
        }
        uint8_t flags = 0;
        if (have) {
            const int16_t step = K0Meaning<Inbox>::i16(frame.bytes+2);
            const int16_t lo = K0Meaning<Inbox>::i16(frame.bytes+4), hi = K0Meaning<Inbox>::i16(frame.bytes+6);
            if (step < 1 || step > 127 || lo > hi) flags |= 4;
            else { increment = step; lower = lo; upper = hi; sequence = K0Meaning<Inbox>::u16(frame.bytes); }
        }
        Gate::point(2,0);
        position += int32_t(amount)*increment;
        if (position < lower) position = lower;
        if (position > upper) position = upper;
        uint8_t next[8]{};
        for (unsigned i = 0; i < 4; ++i) next[i] = uint8_t(uint32_t(position)>>(8*i));
        next[4] = uint8_t(sequence); next[5] = uint8_t(sequence>>8);
        {
            Scope scope;
            status |= flags; next[6] = status;
            for (unsigned i = 0; i < 8; ++i) {
                published[i] = next[i];
                Gate::point(3,i); // Probe only: target policy compiles this out.
            }
        }
        Gate::point(4,0);
    }
};

template<unsigned Candidate,unsigned Capacity,class Gate> struct K0Shared
    : K0SharedMeaning<typename std::conditional<Candidate==1,K0DirectInbox<Capacity>,Queue<K0Frame,Capacity>>::type,Gate> {
    static_assert(Candidate>=1 && Candidate<=4,"four experiment candidates");
    static_assert(Capacity==1 || Capacity==2,"fixed K0 capacities");
    using Self = K0Shared;
    using Work = WorkExecutor<1>;
    // C-bridge is an explicit new comparison: IRQ data live in the protected
    // inbox, normal-context targeted notifications are coalesced, not frames.
    using Events = EventExecutor<uint8_t,1,1>;
    struct Empty {};
    typename std::conditional<Candidate==3,Work,typename std::conditional<Candidate==4,Events,Empty>::type>::type executor;
    typename std::conditional<Candidate==3,typename Work::Token,typename std::conditional<Candidate==4,typename Events::Token,Empty>::type>::type target{};
    bool initialized = false, opted = false;
    WorkDomain domain = WorkDomain::System;
    static void work(void* context,typename Work::Token) {
        auto& self = *static_cast<Self*>(context); Gate::normal(); self.step();
    }
    static void event(void* context,const typename Events::Delivery&) {
        auto& self = *static_cast<Self*>(context); Gate::normal(); self.step();
    }
    void begin(WorkDomain d = WorkDomain::System,bool opt = false) {
        Gate::normal();
        if (initialized) return;
        domain = d; opted = opt;
        if constexpr (Candidate==3) { target = executor.add(work,this,d,opt); executor.initialize(); }
        if constexpr (Candidate==4) { target = executor.add(event,this,d,opt); executor.initialize(); }
        initialized = true;
    }
    void begin_write() { K0SharedMeaning<typename std::conditional<Candidate==1,K0DirectInbox<Capacity>,Queue<K0Frame,Capacity>>::type,Gate>::begin_write(Capacity); }
    TurnStatus opportunity(WorkOpportunity where,WorkProfile profile,WorkContext context = WorkContext::Normal) {
        if (!initialized) return TurnStatus::NotInitialized;
        if (context==WorkContext::Isr || context==WorkContext::Setup) return TurnStatus::Forbidden;
        if (context==WorkContext::Callback) return TurnStatus::Reentrant;
        Gate::normal();
        const bool ready = this->needs_step();
        if constexpr (Candidate>=3) {
            if (ready) {
                if constexpr (Candidate==3) executor.post(target);
                else executor.notify(target,1);
            }
            return executor.turn(1,where,profile,context).status;
        } else {
            (void)ready;
            if (where!=WorkOpportunity::Explicit) {
                if (profile==WorkProfile::M) return TurnStatus::Suppressed;
                if (domain==WorkDomain::User && (profile!=WorkProfile::U || !opted)) return TurnStatus::Ran;
            }
            this->step(); return TurnStatus::Ran;
        }
    }
};
} // namespace experiment
