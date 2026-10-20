#pragma once
#include <limits>
#include <type_traits>

namespace experiment {
enum class WorkDomain { System, User };
enum class WorkProfile { M, S, U };
enum class WorkOpportunity { Explicit, AfterLoop, Delay };
enum class WorkContext { Normal, Setup, Callback, Isr };
enum class TurnStatus { Ran, NotInitialized, Forbidden, Reentrant, Suppressed };

// RUN-01/02 host/compile comparison: serial normal context only. No tick,
// allocation, preemption, independent stack or implicit interrupt posting.
template<unsigned N, class Generation = unsigned>
class WorkExecutor {
    static_assert(N > 0 && N <= 65535, "bounded targets");
    static_assert(std::is_unsigned<Generation>::value &&
                  !std::is_same<Generation, bool>::value, "unsigned generation");
public:
    struct Token { unsigned index; Generation generation; };
    using Handler = void (*)(void*, Token);
    struct Turn { TurnStatus status; unsigned selected, delivered; };
private:
    struct Entry {
        Handler handler = nullptr;
        void* context = nullptr;
        Generation generation = 0;
        WorkDomain domain = WorkDomain::System;
        bool opted_in = false, ready = false;
    } entries[N];
    unsigned cursor = 0;
    bool initialized = false, running = false;
    bool eligible(const Entry& e, WorkOpportunity opportunity, WorkProfile profile) const {
        if (opportunity == WorkOpportunity::Explicit) return true;
        return e.domain == WorkDomain::System || (profile == WorkProfile::U && e.opted_in);
    }
public:
    WorkExecutor() = default;
    WorkExecutor(const WorkExecutor&) = delete;
    WorkExecutor& operator=(const WorkExecutor&) = delete;
    static constexpr Token invalid() { return {N, 0}; }
    bool valid(Token t) const {
        return t.index < N && t.generation != 0 && entries[t.index].handler &&
               entries[t.index].generation == t.generation;
    }
    Token add(Handler handler, void* context, WorkDomain domain, bool opted_in = false) {
        if (!handler) return invalid();
        for (unsigned i = 0; i < N; ++i) {
            auto& e = entries[i];
            if (e.handler || e.generation == std::numeric_limits<Generation>::max()) continue;
            ++e.generation; e.handler = handler; e.context = context;
            e.domain = domain; e.opted_in = opted_in; e.ready = false;
            return {i, e.generation};
        }
        return invalid();
    }
    bool remove(Token t) {
        if (!valid(t)) return false;
        auto& e = entries[t.index]; e.handler = nullptr; e.context = nullptr; e.ready = false;
        return true;
    }
    bool post(Token t) {
        if (!valid(t)) return false;
        entries[t.index].ready = true; return true;
    }
    bool ready(Token t) const { return valid(t) && entries[t.index].ready; }
    void initialize() { initialized = true; }
    Turn turn(unsigned limit, WorkOpportunity opportunity, WorkProfile profile,
              WorkContext context = WorkContext::Normal) {
        if (!initialized) return {TurnStatus::NotInitialized, 0, 0};
        if (context == WorkContext::Isr || context == WorkContext::Setup)
            return {TurnStatus::Forbidden, 0, 0};
        if (running || context == WorkContext::Callback) return {TurnStatus::Reentrant, 0, 0};
        if (opportunity != WorkOpportunity::Explicit && profile == WorkProfile::M)
            return {TurnStatus::Suppressed, 0, 0};
        // Detach this turn's ready set before invoking any handler. New posts
        // survive even when their target is already selected later this turn.
        Token snapshot[N];
        unsigned count = 0, last = cursor;
        for (unsigned offset = 0; offset < N && count < limit; ++offset) {
            const unsigned i = (cursor + offset) % N;
            auto& e = entries[i];
            if (e.handler && e.ready && eligible(e, opportunity, profile)) {
                snapshot[count++] = {i, e.generation}; e.ready = false; last = i;
            }
        }
        if (count) cursor = (last + 1) % N;
        running = true;
        struct Reset { bool& running; ~Reset() { running = false; } } reset{running};
        unsigned delivered = 0;
        for (unsigned i = 0; i < count; ++i) {
            const auto t = snapshot[i];
            if (!valid(t)) continue;
            const auto handler = entries[t.index].handler;
            void* const user = entries[t.index].context;
            ++delivered; handler(user, t);
        }
        return {TurnStatus::Ran, count, delivered};
    }
};
} // namespace experiment
