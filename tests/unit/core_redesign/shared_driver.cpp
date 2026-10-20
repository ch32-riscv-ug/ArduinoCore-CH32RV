#include "owned_queue.hpp"
#include "shared_channels.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using experiment::ChannelResult;
namespace {
std::array<bool, 32> resources{};
unsigned constructed = 0, destroyed = 0, released = 0;
std::vector<int> release_order;
struct alignas(64) Resource {
    int id = -1;
    explicit Resource(int i = -1) noexcept : id(i) {
        assert(reinterpret_cast<uintptr_t>(this) % 64 == 0);
        if (id >= 0) { assert(!resources[id]); resources[id] = true; }
        ++constructed;
    }
    Resource(const Resource&) = delete;
    Resource(Resource&& other) noexcept : id(other.id) { other.id = -1; ++constructed; }
    void reset() noexcept {
        if (id >= 0) {
            assert(resources[id]); resources[id] = false;
            release_order.push_back(id); ++released; id = -1;
        }
    }
    Resource& operator=(Resource&& other) noexcept {
        assert(this != &other); reset(); id = other.id; other.id = -1; return *this;
    }
    ~Resource() noexcept { reset(); ++destroyed; }
};

void owned(unsigned seed) {
    static_assert(!std::is_copy_constructible<experiment::OwnedQueue<Resource, 4>>::value);
    static_assert(!std::is_move_constructible<experiment::OwnedQueue<Resource, 4>>::value);
    std::mt19937 rng(seed);
    std::deque<int> expected;
    unsigned acquired = 0;
    {
        experiment::OwnedQueue<Resource, 4> q;
        Resource output;
        for (unsigned step = 0; step < 20000; ++step) {
            const unsigned op = rng() % 5;
            if (op <= 2) {
                int id = 0; while (resources[id]) ++id;
                const bool room = expected.size() < 4;
                if (op == 0) {
                    const unsigned before = constructed;
                    assert(q.emplace(id) == room);
                    assert(constructed == before + unsigned(room));
                    if (room) ++acquired;
                } else {
                    Resource input(id); ++acquired;
                    assert(q.push(std::move(input)) == room);
                    assert(input.id == (room ? -1 : id));
                }
                if (room) expected.push_back(id);
            } else if (op == 3) {
                const int old = output.id;
                assert(q.pop(output) == !expected.empty());
                if (!expected.empty()) {
                    assert(output.id == expected.front()); expected.pop_front();
                } else assert(output.id == old);
            } else {
                const auto before = release_order.size();
                q.clear();
                assert(release_order.size() == before + expected.size());
                for (unsigned i = 0; i < expected.size(); ++i)
                    assert(release_order[before + i] == expected[i]);
                expected.clear();
            }
            assert(q.size() == expected.size());
        }
        // Ensure scope destruction is exercised with a nonempty queue.
        q.clear(); expected.clear(); output.reset();
        for (int i = 0; i < 4; ++i) { assert(q.emplace(i)); ++acquired; }
        release_order.clear();
    }
    assert((release_order == std::vector<int>{0, 1, 2, 3}));
    assert(constructed == destroyed && released == acquired);
    for (bool live : resources) assert(!live);
}

// Simulates a single core: pending IRQs run only after the outermost saved
// interrupt state is restored. std::function is instrumentation, not product.
struct FakeIrqGate {
    using Token = bool;
    bool enabled = true;
    std::deque<std::function<void()>> deferred;
    bool try_enter(Token& token) { token = enabled; enabled = false; return true; }
    void leave(Token token) {
        enabled = token;
        while (enabled && !deferred.empty()) {
            auto callback = std::move(deferred.front()); deferred.pop_front(); callback();
        }
    }
    void interrupt(std::function<void()> callback) {
        if (enabled) {
            const bool saved = enabled; enabled = false; callback(); leave(saved);
        } else deferred.push_back(std::move(callback));
    }
};
struct Inject {
    unsigned target;
    std::function<void()>* callback;
    void at(unsigned point) { if (point == target) (*callback)(); }
};
void irq(unsigned point) {
    FakeIrqGate gate;
    bool outer, inner;
    assert(gate.try_enter(outer) && outer);
    assert(gate.try_enter(inner) && !inner);
    gate.leave(inner); assert(!gate.enabled);
    gate.leave(outer); assert(gate.enabled);
    gate.enabled = false;
    assert(gate.try_enter(outer)); gate.leave(outer); assert(!gate.enabled);
    gate.enabled = true;
    std::function<void()> injection;
    experiment::GuardedChannel<unsigned, 2, FakeIrqGate, Inject> q(gate, {point, &injection});
    unsigned arrivals = 0;
    injection = [&] { gate.interrupt([&] { ++arrivals; assert(q.push(42) == ChannelResult::Ok); }); };
    const auto result = q.prepare_wait();
    assert(arrivals == 1 && gate.enabled);
    bool wake = false;
    assert(q.take_wake(wake) == ChannelResult::Ok);
    assert(point == 0 ? result == ChannelResult::Ready && !wake
                      : result == ChannelResult::Armed && wake);
    unsigned value = 99;
    assert(q.pop(value) == ChannelResult::Ok && value == 42);
    assert(q.pop(value) == ChannelResult::Empty && value == 42);
    assert(q.take_wake(wake) == ChannelResult::Ok && !wake);
    // Repeated arming, coalescing, full rejection and arrival after rearming.
    injection = [] {};
    assert(q.prepare_wait() == ChannelResult::Armed);
    assert(q.prepare_wait() == ChannelResult::Armed);
    assert(q.push(1) == ChannelResult::Ok && q.push(2) == ChannelResult::Ok);
    assert(q.push(3) == ChannelResult::Full);
    assert(q.take_wake(wake) == ChannelResult::Ok && wake);
    assert(q.prepare_wait() == ChannelResult::Ready);
    assert(q.pop(value) == ChannelResult::Ok && value == 1);
    assert(q.pop(value) == ChannelResult::Ok && value == 2);
    assert(q.prepare_wait() == ChannelResult::Armed);
    gate.interrupt([&] { assert(q.push(4) == ChannelResult::Ok); });
    assert(q.take_wake(wake) == ChannelResult::Ok && wake);
}

struct MutexGate {
    using Token = bool;
    std::mutex mutex;
    bool try_enter(Token& token) { return token = mutex.try_lock(); }
    void leave(Token) { mutex.unlock(); }
};
struct Payload {
    unsigned producer = 0, sequence = 0, inverse = 0, check = 0;
};
Payload payload(unsigned p, unsigned s) { return {p, s, ~s, (p + 17) * 65537u ^ s}; }
void verify(const Payload& value, unsigned p, unsigned s) {
    assert(value.producer == p && value.sequence == s && value.inverse == ~s);
    assert(value.check == ((p + 17) * 65537u ^ s));
}
void mpsc(unsigned seed) {
    MutexGate gate;
    experiment::GuardedChannel<Payload, 4, MutexGate> q(gate);
    // Busy must preserve both output and wait registration.
    std::thread holder([&] {
        MutexGate::Token token;
        assert(gate.try_enter(token));
        std::thread caller([&] {
            Payload value = payload(9, 99); bool wake = true;
            assert(q.push(value) == ChannelResult::Busy);
            assert(q.pop(value) == ChannelResult::Busy); verify(value, 9, 99);
            assert(q.prepare_wait() == ChannelResult::Busy);
            assert(q.take_wake(wake) == ChannelResult::Busy && wake);
        });
        caller.join(); gate.leave(token);
    });
    holder.join();
    constexpr unsigned count = 25000, producers = 4;
    std::array<std::thread, producers> threads;
    for (unsigned p = 0; p < producers; ++p) threads[p] = std::thread([&, p] {
        std::mt19937 rng(seed + p);
        for (unsigned i = 0; i < count; ++i) {
            const auto value = payload(p, i);
            while (q.push(value) != ChannelResult::Ok) std::this_thread::yield();
            if (rng() % 31 == 0) std::this_thread::yield();
        }
    });
    std::array<unsigned, producers> next{};
    for (unsigned i = 0; i < count * producers;) {
        Payload value;
        if (q.pop(value) == ChannelResult::Ok) {
            assert(value.producer < producers);
            verify(value, value.producer, next[value.producer]++); ++i;
        } else std::this_thread::yield();
    }
    for (auto& thread : threads) thread.join();
    for (auto n : next) assert(n == count);
}
template<unsigned N, class Counter> void spsc(unsigned seed) {
    experiment::SpscChannel<Payload, N, Counter> q;
    Payload value = payload(9, 99);
    assert(!q.pop(value)); verify(value, 9, 99);
    for (unsigned i = 0; i < N; ++i) assert(q.push(payload(0, i)));
    assert(!q.push(payload(0, N)));
    for (unsigned i = 0; i < N; ++i) { assert(q.pop(value)); verify(value, 0, i); }
    constexpr unsigned count = 100000;
    std::thread producer([&] {
        std::mt19937 rng(seed);
        for (unsigned i = 0; i < count; ++i) {
            while (!q.push(payload(0, i))) std::this_thread::yield();
            if (rng() % 31 == 0) std::this_thread::yield();
        }
    });
    for (unsigned i = 0; i < count; ++i) {
        while (!q.pop(value)) std::this_thread::yield();
        verify(value, 0, i);
    }
    producer.join(); assert(!q.pop(value));
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 3);
    const std::string mode = argv[1];
    const unsigned seed = unsigned(std::strtoul(argv[2], nullptr, 10));
    if (mode == "owned") owned(seed);
    else if (mode == "irq") irq(seed);
    else if (mode == "mpsc") mpsc(seed);
    else if (mode == "spsc1") spsc<1, uint8_t>(seed);
    else if (mode == "spsc4") spsc<4, uint8_t>(seed);
    else if (mode == "spsc64") spsc<64, uint32_t>(seed);
    else return 2;
}
