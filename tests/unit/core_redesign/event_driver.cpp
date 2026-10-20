#include "event_executor.hpp"
#include "io_lifecycle.hpp"
#include "deadline_timer.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace experiment;
struct Payload { uint32_t sequence = 0, inverse = 0, origin = 0, check = 0; };
Payload payload(unsigned value, unsigned origin) { return {value, ~uint32_t(value), origin, value ^ (origin * 65537u)}; }
template<unsigned N, unsigned M> struct Fixture {
    using E = EventExecutor<Payload, N, M>;
    E executor;
    typename E::Token tokens[N];
    std::vector<typename E::Delivery> calls;
    std::vector<unsigned> sends;
    bool self = false, cross = false, nested = false;
    static void handler(void* context, const typename E::Delivery& d) {
        auto& f = *static_cast<Fixture*>(context);
        if (d.has_message) {
            assert(d.message.inverse == ~d.message.sequence);
            assert(d.message.check == (d.message.sequence ^ (d.message.origin * 65537u)));
        }
        f.calls.push_back(d);
        const unsigned value = d.has_message ? d.message.sequence : 42;
        if (f.self) {
            f.sends.push_back(unsigned(f.executor.send(d.target, payload(value + 1, d.target.index))));
            assert(f.executor.notify(d.target, 1));
        }
        if (f.cross) {
            const auto target = f.tokens[(d.target.index + 1) % N];
            f.sends.push_back(unsigned(f.executor.send(target, payload(value + 10, target.index))));
            assert(f.executor.notify(target, 2));
        }
        if (f.nested) for (auto opportunity : {WorkOpportunity::Explicit, WorkOpportunity::AfterLoop, WorkOpportunity::Delay})
            assert(f.executor.turn(N, opportunity, WorkProfile::U).status == TurnStatus::Reentrant);
    }
    explicit Fixture(bool mixed = false) {
        for (unsigned i = 0; i < N; ++i)
            tokens[i] = executor.add(handler, this, mixed && i == 0 ? WorkDomain::System : WorkDomain::User,
                                     !mixed || i == 1);
    }
};
template<unsigned N> void finite(unsigned quota) {
    Fixture<N, 1> f; f.executor.initialize(); f.self = f.cross = f.nested = true;
    for (auto token : f.tokens) assert(f.executor.notify(token, 8));
    unsigned cursor = 0;
    for (unsigned i = 0; i < 100; ++i) {
        f.calls.clear();
        const auto r = f.executor.turn(quota, WorkOpportunity::Explicit, WorkProfile::M);
        assert(r.delivered == (quota < N ? quota : N));
        for (const auto& d : f.calls) { assert(d.target.index == cursor); cursor = (cursor + 1) % N; }
    }
    Fixture<2, 1> g; g.executor.initialize(); g.cross = true;
    assert(g.executor.notify(g.tokens[0], 8) && g.executor.notify(g.tokens[1], 8));
    assert(g.executor.send(g.tokens[1], payload(9, 1)) == EventSend::Accepted);
    g.executor.turn(2, WorkOpportunity::Explicit, WorkProfile::M);
    assert(g.calls.size() == 2 && !g.calls[0].has_message && g.calls[1].message.sequence == 9);
    assert(g.sends[0] == unsigned(EventSend::Full)); // Its slot was occupied until its own delivery.
    assert(g.executor.notifications(g.tokens[1]) == 2); // New bits survive the old snapshot.
}
void profile() {
    Fixture<3, 1> f(true);
    assert(f.executor.notify(f.tokens[0], 1));
    assert(f.executor.turn(3, WorkOpportunity::Delay, WorkProfile::U).status == TurnStatus::NotInitialized);
    f.executor.initialize();
    for (auto context : {WorkContext::Setup, WorkContext::Isr, WorkContext::Callback}) {
        auto r = f.executor.turn(3, WorkOpportunity::Explicit, WorkProfile::U, context);
        assert(r.status == (context == WorkContext::Callback ? TurnStatus::Reentrant : TurnStatus::Forbidden));
        assert(f.executor.notifications(f.tokens[0]) == 1);
    }
    for (auto opportunity : {WorkOpportunity::AfterLoop, WorkOpportunity::Delay})
        for (auto p : {WorkProfile::M, WorkProfile::S, WorkProfile::U}) {
            for (auto t : f.tokens) assert(f.executor.notify(t, 1));
            f.calls.clear(); auto r = f.executor.turn(3, opportunity, p);
            assert(r.delivered == (p == WorkProfile::M ? 0u : p == WorkProfile::S ? 1u : 2u));
            for (const auto& d : f.calls) assert(d.target.index == 0 || (p == WorkProfile::U && d.target.index == 1));
            assert(f.executor.notifications(f.tokens[2]) == 1);
            f.executor.turn(3, WorkOpportunity::Explicit, p);
            for (auto t : f.tokens) assert(!f.executor.notifications(t));
        }
}
void fanout(unsigned count) {
    Fixture<4, 1> f; f.executor.initialize();
    assert(f.executor.send(f.tokens[1], payload(99, 1)) == EventSend::Accepted);
    for (unsigned i = 0; i < count; ++i)
        assert(f.executor.send(f.tokens[i], payload(17, i)) == (i == 1 ? EventSend::Full : EventSend::Accepted));
    assert(f.calls.empty()); f.executor.turn(4, WorkOpportunity::Explicit, WorkProfile::M);
    assert(f.calls.size() == (count == 1 ? 2 : count));
    for (const auto& d : f.calls) assert(d.message.sequence == (d.target.index == 1 ? 99u : 17u));
}
void lifetime() {
    EventExecutor<Payload, 1, 1, uint8_t> e;
    auto handler = [](void*, const decltype(e)::Delivery&) {};
    e.initialize(); auto old = e.invalid();
    for (unsigned i = 1; i <= 255; ++i) {
        auto t = e.add(handler, nullptr, WorkDomain::User);
        assert(e.valid(t) && t.generation == i);
        assert(!e.valid(old) && e.send(old, payload(1, 0)) == EventSend::Stale && !e.notify(old, 1));
        assert(e.send(t, payload(i, 0)) == EventSend::Accepted && e.notify(t, 1));
        assert(e.remove(t)); old = t;
        assert(e.turn(1, WorkOpportunity::Explicit, WorkProfile::M).delivered == 0);
    }
    assert(!e.valid(e.add(handler, nullptr, WorkDomain::User)));
}
struct Bridge {
    using E = EventExecutor<Payload, 2, 1>;
    E executor;
    E::Token blocker, target;
    IoSlot<32> io; IoBuffer<32> buffer; DeadlineTimer timer;
    unsigned io_calls = 0, completions = 0, expirations = 0, messages = 0;
    uint32_t skipped = 0;
    bool close_in_blocker = false;
    static void on_io(void* context, const E::Delivery& d) {
        auto& b = *static_cast<Bridge*>(context); ++b.io_calls;
        if (d.has_message) ++b.messages;
        if (d.notifications & 1) {
            IoSlot<32>::Completion completion;
            if (b.io.take(b.io.token(), completion)) { assert(completion.result == IoResult::Success); ++b.completions; }
        }
        if (d.notifications & 2) {
            DeadlineTimer::Expiration expiration;
            if (b.timer.take(expiration)) { ++b.expirations; b.skipped = expiration.skipped; }
        }
    }
    static void on_blocker(void* context, const E::Delivery&) {
        auto& b = *static_cast<Bridge*>(context);
        if (!b.close_in_blocker) return;
        const auto old = b.target;
        assert(b.io.end() && b.executor.remove(old));
        b.target = b.executor.add(on_io, &b, WorkDomain::System);
        assert(b.target.index == old.index && b.target.generation != old.generation);
        assert(b.executor.notify(b.target, 4));
    }
    Bridge() {
        blocker = executor.add(on_blocker, this, WorkDomain::User);
        target = executor.add(on_io, this, WorkDomain::System);
        executor.initialize(); assert(io.open());
    }
    void complete() {
        assert(io.start(&buffer, 8, IoMode::Read, 64) == IoStart::Accepted);
        assert(io.hardware_terminal(io.token(), IoResult::Success, 8, 250));
        IoSlot<32>::Token token;
        assert(io.peek_notice(token));
        assert(executor.notify(target, 1) && io.ack_notice(token));
    }
};
void bridge(bool close) {
    Bridge b;
    assert(b.executor.send(b.target, payload(99, 1)) == EventSend::Accepted);
    assert(b.executor.send(b.target, payload(100, 1)) == EventSend::Full);
    b.complete();
    if (close) {
        b.close_in_blocker = true; assert(b.executor.notify(b.blocker, 1));
        auto r = b.executor.turn(2, WorkOpportunity::Explicit, WorkProfile::M);
        assert(r.selected == 2 && r.delivered == 1 && !b.io_calls && !b.buffer.available());
        IoSlot<32>::Completion completion;
        assert(b.io.take(b.io.token(), completion) && b.io.open());
        b.complete();
        b.executor.turn(2, WorkOpportunity::Explicit, WorkProfile::M);
        assert(b.completions == 1 && b.io_calls == 1 && b.messages == 0 && b.buffer.available());
    } else {
        assert(b.timer.arm(0, 0, 10) && b.timer.update(35));
        assert(b.executor.notify(b.target, 2));
        assert(b.executor.turn(2, WorkOpportunity::Delay, WorkProfile::M).status == TurnStatus::Suppressed);
        assert(!b.buffer.available() && b.io.terminal() && b.timer.ready());
        b.executor.turn(2, WorkOpportunity::Delay, WorkProfile::S);
        assert(b.completions == 1 && b.messages == 1 && b.expirations == 1 && b.skipped == 3 && b.buffer.available());
        assert(b.timer.arm(40, 0) && b.timer.update(40));
        assert(b.executor.notify(b.target, 2));
        assert(b.timer.arm(100, 5)); // Old bit may arrive, but old expiry is no longer current.
        b.executor.turn(2, WorkOpportunity::Explicit, WorkProfile::M); assert(b.expirations == 1);
        assert(b.timer.update(105) && b.executor.notify(b.target, 2));
        b.executor.turn(2, WorkOpportunity::Explicit, WorkProfile::M); assert(b.expirations == 2);
    }
}
template<unsigned N, unsigned M> void protocol() {
    Fixture<N, M> f; f.executor.initialize();
    std::string command;
    while (std::cin >> command) {
        unsigned result = 0; f.calls.clear(); f.sends.clear();
        if (command == "send") {
            unsigned i, value; std::cin >> i >> value; assert(i < N);
            result = unsigned(f.executor.send(f.tokens[i], payload(value, i)));
        } else if (command == "notify") {
            unsigned i, bits; std::cin >> i >> bits; assert(i < N); result = f.executor.notify(f.tokens[i], bits);
        } else if (command == "mode") std::cin >> f.self >> f.cross;
        else {
            assert(command == "turn"); unsigned limit; std::cin >> limit;
            auto r = f.executor.turn(limit, WorkOpportunity::Explicit, WorkProfile::M);
            result = unsigned(r.status) * 10000 + r.selected * 100 + r.delivered;
        }
        std::cout << result << '|';
        for (const auto& d : f.calls) std::cout << d.target.index << ',' << d.has_message << ','
            << (d.has_message ? d.message.sequence : 0) << ',' << d.notifications << ';';
        std::cout << '|'; for (auto r : f.sends) std::cout << r << ',';
        std::cout << '|';
        for (auto t : f.tokens) std::cout << f.executor.queued(t) << ',' << f.executor.notifications(t) << ';';
        std::cout << '\n';
    }
}
template<unsigned N> void capacity(unsigned m) {
    if (m == 1) protocol<N, 1>(); else if (m == 4) protocol<N, 4>(); else { assert(m == 8); protocol<N, 8>(); }
}
int main(int argc, char** argv) {
    assert(argc >= 2); const std::string mode = argv[1];
    if (mode == "profile") profile();
    else if (mode == "lifetime") lifetime();
    else if (mode == "bridge") { assert(argc == 3); bridge(std::strtoul(argv[2], nullptr, 10)); }
    else if (mode == "fanout") { assert(argc == 3); fanout(unsigned(std::strtoul(argv[2], nullptr, 10))); }
    else {
        assert(argc == 4); const auto n = std::strtoul(argv[2], nullptr, 10);
        const auto m = unsigned(std::strtoul(argv[3], nullptr, 10));
        if (mode == "finite") { if (n == 1) finite<1>(m); else if (n == 4) finite<4>(m); else { assert(n == 8); finite<8>(m); } }
        else { assert(mode == "protocol"); if (n == 1) capacity<1>(m); else if (n == 4) capacity<4>(m); else { assert(n == 8); capacity<8>(m); } }
    }
}
