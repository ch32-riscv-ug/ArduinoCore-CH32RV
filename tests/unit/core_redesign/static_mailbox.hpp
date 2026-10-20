#pragma once
#include "event_executor.hpp"

namespace experiment {
// Additional C binding: one static destination, one bounded snapshot batch.
// No dynamic subscriptions or stored callbacks. Serial context only.
template<class Message, unsigned Capacity> class StaticMailbox {
    Queue<Message, Capacity> queue;
    uint32_t bits = 0;
    WorkDomain domain = WorkDomain::System;
    bool initialized = false, opted = false, running = false;
public:
    struct Batch { Message messages[Capacity]{}; unsigned count = 0; uint32_t notifications = 0; };
    StaticMailbox() = default;
    StaticMailbox(const StaticMailbox&) = delete;
    StaticMailbox& operator=(const StaticMailbox&) = delete;
    bool begin(WorkDomain d = WorkDomain::System, bool opt = false) {
        if (initialized) return false;
        domain = d; opted = opt; initialized = true; return true;
    }
    unsigned size() const { return queue.size(); }
    EventSend send(const Message& message) {
        if (!initialized) return EventSend::Stale;
        return queue.push(message) ? EventSend::Accepted : EventSend::Full;
    }
    bool notify(uint32_t flags) {
        if (!initialized || !flags) return false;
        bits |= flags; return true;
    }
    template<class Handler> TurnStatus deliver(WorkOpportunity where, WorkProfile profile, WorkContext context, Handler handler) {
        if (!initialized) return TurnStatus::NotInitialized;
        if (context == WorkContext::Setup || context == WorkContext::Isr) return TurnStatus::Forbidden;
        if (running || context == WorkContext::Callback) return TurnStatus::Reentrant;
        if (where != WorkOpportunity::Explicit) {
            if (profile == WorkProfile::M) return TurnStatus::Suppressed;
            if (domain == WorkDomain::User && (profile != WorkProfile::U || !opted)) return TurnStatus::Ran;
        }
        if (!queue.size() && !bits) return TurnStatus::Ran;
        Batch batch;
        batch.count = queue.size(); batch.notifications = bits; bits = 0;
        for (unsigned i = 0; i < batch.count; ++i) queue.pop(batch.messages[i]);
        running = true;
        struct Reset { bool& running; ~Reset() { running = false; } } reset{running};
        handler(batch);
        return TurnStatus::Ran;
    }
};
} // namespace experiment
