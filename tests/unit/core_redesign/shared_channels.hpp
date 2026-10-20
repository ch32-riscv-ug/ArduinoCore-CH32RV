#pragma once
#include "data_parts.hpp"
#include <atomic>
#include <limits>
#include <type_traits>

namespace experiment {
enum class ChannelResult { Ok, Busy, Full, Empty, Ready, Armed };
struct NoProbe { void at(unsigned) {} };

// Gate supplies try_enter(Token&) and leave(Token). It owns the synchronization
// semantics; an IRQ gate is not a multicore lock. Probe is test instrumentation.
template<class T, unsigned N, class Gate, class Probe = NoProbe>
class GuardedChannel : private Probe {
    Gate& gate;
    Queue<T, N> queue;
    bool armed = false, pending = false;
    struct Scope {
        Gate& gate;
        typename Gate::Token token{};
        bool entered;
        explicit Scope(Gate& g) : gate(g), entered(g.try_enter(token)) {}
        ~Scope() { if (entered) gate.leave(token); }
    };
public:
    constexpr explicit GuardedChannel(Gate& g, Probe p = {}) : Probe(p), gate(g) {}
    GuardedChannel(const GuardedChannel&) = delete;
    GuardedChannel& operator=(const GuardedChannel&) = delete;
    ChannelResult push(const T& value) {
        Scope scope(gate);
        if (!scope.entered) return ChannelResult::Busy;
        if (!queue.push(value)) return ChannelResult::Full;
        if (armed) { pending = true; armed = false; }
        return ChannelResult::Ok;
    }
    ChannelResult pop(T& value) {
        Scope scope(gate);
        if (!scope.entered) return ChannelResult::Busy;
        return queue.pop(value) ? ChannelResult::Ok : ChannelResult::Empty;
    }
    ChannelResult prepare_wait() {
        Probe::at(0);
        Scope scope(gate);
        if (!scope.entered) return ChannelResult::Busy;
        pending = false; Probe::at(1);
        if (queue.size()) { armed = false; return ChannelResult::Ready; }
        Probe::at(2);
        armed = true; Probe::at(3); Probe::at(4);
        return ChannelResult::Armed;
    }
    ChannelResult take_wake(bool& value) {
        Scope scope(gate);
        if (!scope.entered) return ChannelResult::Busy;
        value = pending; pending = false;
        return ChannelResult::Ok;
    }
};

// Exactly one producer and one consumer. Neither side may be re-entered.
// Acquire/release publishes payload and releases reusable slots. MCU cache,
// shared-memory placement and reset ownership are separate backend obligations.
template<class T, unsigned N, class Counter = unsigned>
class SpscChannel {
    static_assert(std::is_unsigned<Counter>::value && !std::is_same<Counter, bool>::value,
                  "unsigned counter required");
    static_assert(N > 0 && (N & (N - 1)) == 0 &&
                  N <= std::numeric_limits<Counter>::max() / 2, "bounded power-of-two capacity");
    static_assert(std::is_trivially_copyable<T>::value &&
                  std::is_nothrow_copy_assignable<T>::value, "nonthrowing trivial payload required");
    T storage[N]{};
    std::atomic<Counter> written{0}, consumed{0};
public:
    SpscChannel() = default;
    SpscChannel(const SpscChannel&) = delete;
    SpscChannel& operator=(const SpscChannel&) = delete;
    bool push(const T& value) noexcept {
        const Counter w = written.load(std::memory_order_relaxed);
        const Counter r = consumed.load(std::memory_order_acquire);
        if (Counter(w - r) == N) return false;
        storage[w & (N - 1)] = value;
        written.store(Counter(w + 1), std::memory_order_release);
        return true;
    }
    bool pop(T& value) noexcept {
        const Counter r = consumed.load(std::memory_order_relaxed);
        const Counter w = written.load(std::memory_order_acquire);
        if (r == w) return false;
        value = storage[r & (N - 1)];
        consumed.store(Counter(r + 1), std::memory_order_release);
        return true;
    }
};
} // namespace experiment
