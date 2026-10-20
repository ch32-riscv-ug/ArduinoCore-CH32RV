#include "work_executor.hpp"
#include <cassert>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace experiment;
template<unsigned N> struct Fixture {
    using Executor = WorkExecutor<N>;
    using Token = typename Executor::Token;
    Executor executor;
    Token tokens[N];
    std::vector<unsigned> calls;
    unsigned self = 0, cross = 0;
    bool nested = false;
    static void handler(void* context, Token token) {
        auto& f = *static_cast<Fixture*>(context);
        f.calls.push_back(token.index);
        if (f.self) assert(f.executor.post(token));
        if (f.cross) assert(f.executor.post(f.tokens[(token.index + 1) % N]));
        if (f.nested) {
            for (auto opportunity : {WorkOpportunity::Explicit, WorkOpportunity::AfterLoop, WorkOpportunity::Delay}) {
                const auto r = f.executor.turn(N, opportunity, WorkProfile::U);
                assert(r.status == TurnStatus::Reentrant && !r.delivered);
            }
        }
    }
    Fixture() {
        for (unsigned i = 0; i < N; ++i) {
            tokens[i] = executor.add(handler, this, WorkDomain::User, true);
            assert(executor.valid(tokens[i]));
        }
        executor.initialize();
    }
};

template<unsigned N> void finite(unsigned limit) {
    Fixture<N> f; f.self = f.cross = 1; f.nested = true;
    for (auto token : f.tokens) assert(f.executor.post(token));
    for (unsigned i = 0; i < 100; ++i) {
        const auto before = f.calls.size();
        const auto turn = f.executor.turn(limit, WorkOpportunity::Explicit, WorkProfile::M);
        const unsigned expected = limit < N ? limit : N;
        assert(turn.status == TurnStatus::Ran && turn.selected == expected && turn.delivered == expected);
        for (unsigned j = 0; j < expected; ++j)
            assert(f.calls[before + j] == (before + j) % N);
    }
    // Mutual posts to a target not originally ready cannot run in this turn.
    Fixture<N> g; g.cross = 1;
    assert(g.executor.post(g.tokens[0]));
    auto r = g.executor.turn(N, WorkOpportunity::Explicit, WorkProfile::M);
    assert(r.delivered == 1 && g.calls.size() == 1);
    assert(g.executor.ready(g.tokens[N == 1 ? 0 : 1]));
    // A post to an already selected, yet-to-run target survives to next turn.
    Fixture<N> h; h.cross = 1;
    for (auto token : h.tokens) assert(h.executor.post(token));
    assert(h.executor.turn(N, WorkOpportunity::Explicit, WorkProfile::M).delivered == N);
    for (auto token : h.tokens) assert(h.executor.ready(token));
}

void profile() {
    using E = WorkExecutor<3>;
    E e; std::vector<unsigned> calls;
    auto handler = [](void* p, E::Token t) { static_cast<std::vector<unsigned>*>(p)->push_back(t.index); };
    const auto system = e.add(handler, &calls, WorkDomain::System);
    const auto user = e.add(handler, &calls, WorkDomain::User, true);
    const auto private_user = e.add(handler, &calls, WorkDomain::User, false);
    assert(!e.valid(e.add(handler, &calls, WorkDomain::System)));
    assert(e.post(system));
    assert(e.turn(3, WorkOpportunity::Delay, WorkProfile::U).status == TurnStatus::NotInitialized);
    assert(e.ready(system)); e.initialize();
    for (auto context : {WorkContext::Setup, WorkContext::Callback, WorkContext::Isr}) {
        auto r = e.turn(3, WorkOpportunity::Explicit, WorkProfile::U, context);
        assert(r.status == (context == WorkContext::Callback ? TurnStatus::Reentrant : TurnStatus::Forbidden));
        assert(e.ready(system));
    }
    for (auto opportunity : {WorkOpportunity::AfterLoop, WorkOpportunity::Delay}) {
        for (auto p : {WorkProfile::M, WorkProfile::S, WorkProfile::U}) {
            for (auto token : {system, user, private_user}) assert(e.post(token));
            calls.clear(); const auto r = e.turn(3, opportunity, p);
            if (p == WorkProfile::M) assert(r.status == TurnStatus::Suppressed && calls.empty());
            else if (p == WorkProfile::S) assert((calls == std::vector<unsigned>{system.index}));
            else {
                assert(calls.size() == 2);
                assert((calls[0] == system.index || calls[1] == system.index));
                assert((calls[0] == user.index || calls[1] == user.index));
            }
            assert(e.ready(private_user));
            calls.clear(); assert(e.turn(3, WorkOpportunity::Explicit, p).status == TurnStatus::Ran);
            assert(!e.ready(system) && !e.ready(user) && !e.ready(private_user));
        }
    }
    assert(e.post(system));
    assert(e.turn(0, WorkOpportunity::Explicit, WorkProfile::M).selected == 0 && e.ready(system));
}

void lifetime() {
    struct State {
        WorkExecutor<2, unsigned char> executor;
        WorkExecutor<2, unsigned char>::Token first, old, replacement;
        unsigned old_calls = 0, new_calls = 0;
    } s;
    using E = decltype(s.executor);
    auto replacement = [](void* p, E::Token) { ++static_cast<State*>(p)->new_calls; };
    s.old = s.executor.add([](void* p, E::Token) { ++static_cast<State*>(p)->old_calls; }, &s, WorkDomain::User);
    // First registered is intentionally executed first; it removes a selected
    // target then reuses its index. The old snapshot must not call either.
    assert(s.executor.remove(s.old));
    s.first = s.executor.add([](void* p, E::Token) {
        auto& s = *static_cast<State*>(p);
        assert(s.executor.remove(s.old));
        s.replacement = s.executor.add([](void* p, E::Token) { ++static_cast<State*>(p)->new_calls; }, &s, WorkDomain::User);
        assert(s.replacement.index == s.old.index && s.replacement.generation != s.old.generation);
        assert(s.executor.post(s.replacement));
        assert(!s.executor.post(s.old) && !s.executor.remove(s.old));
    }, &s, WorkDomain::User);
    s.old = s.executor.add([](void* p, E::Token) { ++static_cast<State*>(p)->old_calls; }, &s, WorkDomain::User);
    s.executor.initialize(); assert(s.executor.post(s.first) && s.executor.post(s.old));
    auto r = s.executor.turn(2, WorkOpportunity::Explicit, WorkProfile::M);
    assert(r.selected == 2 && r.delivered == 1 && !s.old_calls && !s.new_calls);
    assert(s.executor.turn(2, WorkOpportunity::Explicit, WorkProfile::M).delivered == 1 && s.new_calls == 1);
    assert(s.executor.remove(s.first) && s.executor.remove(s.replacement));
    // Exhaust both 8-bit generation slots rather than wrap and accept ABA.
    for (;;) {
        auto token = s.executor.add(replacement, &s, WorkDomain::User);
        if (!s.executor.valid(token)) break;
        assert(s.executor.remove(token));
    }
    assert(!s.executor.valid(s.old) && !s.executor.post(s.old));
}

// Text protocol for a separate Python readiness/cursor model. Handler only
// logs its identity and optionally reposts; registry lifetimes above are fixed.
template<unsigned N> void protocol() {
    Fixture<N> f;
    std::string command;
    while (std::cin >> command) {
        if (command == "post") { unsigned i; std::cin >> i; assert(i < N); f.executor.post(f.tokens[i]); }
        else if (command == "mode") std::cin >> f.self >> f.cross;
        else {
            assert(command == "turn"); unsigned limit; std::cin >> limit;
            f.calls.clear(); f.executor.turn(limit, WorkOpportunity::Explicit, WorkProfile::M);
        }
        for (auto i : f.calls) std::cout << i << ',';
        std::cout << ':';
        for (auto token : f.tokens) std::cout << int(f.executor.ready(token));
        std::cout << '\n'; f.calls.clear();
    }
}
int main(int argc, char** argv) {
    assert(argc >= 2);
    const std::string mode = argv[1];
    if (mode == "profile") profile();
    else if (mode == "lifetime") lifetime();
    else if (mode == "finite") {
        assert(argc == 4); const auto n = std::strtoul(argv[2], nullptr, 10);
        const auto limit = unsigned(std::strtoul(argv[3], nullptr, 10));
        if (n == 1) finite<1>(limit); else if (n == 4) finite<4>(limit); else { assert(n == 8); finite<8>(limit); }
    } else if (mode == "1") protocol<1>();
    else if (mode == "4") protocol<4>();
    else if (mode == "8") protocol<8>();
    else return 2;
}
