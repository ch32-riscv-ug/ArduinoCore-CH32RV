#pragma once
#include "data_parts.hpp"
#include "work_executor.hpp"

namespace experiment {
enum class EventSend { Accepted, Full, Stale };
// Serial-context targeted-event comparison. FIFO payload and coalesced bits
// have different storage/overflow semantics; neither invokes inline handlers.
template<class Message, unsigned N, unsigned M, class Generation = unsigned>
class EventExecutor {
    static_assert(N > 0 && N <= 65535, "bounded targets");
    static_assert(std::is_unsigned<Generation>::value &&
                  !std::is_same<Generation, bool>::value, "unsigned generation");
    static_assert(std::is_nothrow_default_constructible<Message>::value &&
                  std::is_nothrow_copy_assignable<Message>::value, "nonthrowing payload");
public:
    struct Token { unsigned index; Generation generation; };
    struct Delivery { Token target{}; Message message{}; uint32_t notifications = 0; bool has_message = false; };
    using Handler = void (*)(void*, const Delivery&);
    struct Turn { TurnStatus status; unsigned selected, delivered; };
private:
    struct Entry {
        Queue<Message, M> messages;
        Handler handler = nullptr;
        void* context = nullptr;
        Generation generation = 0;
        uint32_t notifications = 0;
        WorkDomain domain = WorkDomain::System;
        bool opted_in = false;
    } entries[N];
    unsigned cursor = 0;
    bool initialized = false, running = false;
public:
    EventExecutor() = default;
    EventExecutor(const EventExecutor&) = delete;
    EventExecutor& operator=(const EventExecutor&) = delete;
    static constexpr Token invalid() { return {N, 0}; }
    bool valid(Token t) const {
        return t.index < N && t.generation && entries[t.index].handler && entries[t.index].generation == t.generation;
    }
    Token add(Handler handler, void* context, WorkDomain domain, bool opted_in = false) {
        if (!handler) return invalid();
        for (unsigned i = 0; i < N; ++i) {
            auto& e = entries[i];
            if (e.handler || e.generation == std::numeric_limits<Generation>::max()) continue;
            ++e.generation; e.handler = handler; e.context = context;
            e.domain = domain; e.opted_in = opted_in; e.notifications = 0;
            return {i, e.generation};
        }
        return invalid();
    }
    bool remove(Token t) {
        if (!valid(t)) return false;
        auto& e = entries[t.index];
        e.handler = nullptr; e.context = nullptr; e.notifications = 0;
        // Message is trivial: discard queued payloads without user destruction.
        e.messages.head = e.messages.count = 0;
        return true;
    }
    EventSend send(Token t, const Message& value) {
        if (!valid(t)) return EventSend::Stale;
        return entries[t.index].messages.push(value) ? EventSend::Accepted : EventSend::Full;
    }
    bool notify(Token t, uint32_t bits) {
        if (!valid(t) || !bits) return false;
        entries[t.index].notifications |= bits; return true;
    }
    unsigned queued(Token t) const { return valid(t) ? entries[t.index].messages.size() : 0; }
    uint32_t notifications(Token t) const { return valid(t) ? entries[t.index].notifications : 0; }
    void initialize() { initialized = true; }
    Turn turn(unsigned limit, WorkOpportunity opportunity, WorkProfile profile,
              WorkContext context = WorkContext::Normal) {
        if (!initialized) return {TurnStatus::NotInitialized, 0, 0};
        if (context == WorkContext::Isr || context == WorkContext::Setup) return {TurnStatus::Forbidden, 0, 0};
        if (running || context == WorkContext::Callback) return {TurnStatus::Reentrant, 0, 0};
        if (opportunity != WorkOpportunity::Explicit && profile == WorkProfile::M) return {TurnStatus::Suppressed, 0, 0};
        Delivery snapshot[N];
        unsigned count = 0, last = cursor;
        for (unsigned offset = 0; offset < N && count < limit; ++offset) {
            const unsigned i = (cursor + offset) % N;
            auto& e = entries[i];
            const bool eligible = opportunity == WorkOpportunity::Explicit || e.domain == WorkDomain::System ||
                                  (profile == WorkProfile::U && e.opted_in);
            if (!e.handler || !eligible || (!e.notifications && !e.messages.size())) continue;
            auto& d = snapshot[count++]; d.target = {i, e.generation};
            d.notifications = e.notifications; e.notifications = 0;
            d.has_message = e.messages.size() != 0;
            // Keep its queue slot occupied until delivery actually starts.
            // A message arriving during this turn cannot replace this copy.
            if (d.has_message) d.message = e.messages.storage[e.messages.head];
            last = i;
        }
        if (count) cursor = (last + 1) % N;
        running = true;
        struct Reset { bool& running; ~Reset() { running = false; } } reset{running};
        unsigned delivered = 0;
        for (unsigned i = 0; i < count; ++i) {
            const auto& d = snapshot[i];
            if (!valid(d.target)) continue;
            auto& e = entries[d.target.index];
            if (d.has_message) { Message consumed{}; e.messages.pop(consumed); }
            const auto handler = e.handler; void* const user = e.context;
            ++delivered; handler(user, d);
        }
        return {TurnStatus::Ran, count, delivered};
    }
};
} // namespace experiment
