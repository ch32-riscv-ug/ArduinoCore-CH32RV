#include "wait_dependencies.hpp"
#include "event_executor.hpp"
#include <cassert>
#include <iostream>
#include <initializer_list>
#include <string>

using namespace experiment;
constexpr WaitDependencies<4, 3> initial_dependencies;
static_assert(initial_dependencies.owners[0] == -1, "no dynamic graph initialization");
struct Scenario {
    unsigned candidate, mode, where, profile, root_kind, root_opt, helper_kind, helper_opt;
    int owner[3];
    unsigned needs[3], deps[3], known;
    uint32_t timeout, due, release, inject;
    unsigned context, preloaded;
    uint32_t now;
};
template<unsigned Variant> struct Dispatch {};
template<> struct Dispatch<1> { WorkExecutor<3> executor; WorkExecutor<3>::Token token[4]; };
template<> struct Dispatch<2> { EventExecutor<uint32_t, 3, 1> executor; EventExecutor<uint32_t, 3, 1>::Token token[4]; };
template<unsigned Variant> struct Rig : Dispatch<Variant> {
    WaitDependencies<4, 3> graph;
    Queue<uint32_t, 1> result;
    Scenario s;
    WaitPermissions permissions;
    struct Context { Rig* self; unsigned actor; } contexts[4];
    uint32_t origin;
    unsigned drives = 0, normal_calls = 0, produced = 0, done = 0;
    static void work(void* p, WorkExecutor<3>::Token) {
        auto& c = *static_cast<Context*>(p); c.self->run_actor(c.actor);
    }
    static void event(void* p, const EventExecutor<uint32_t, 3, 1>::Delivery&) {
        auto& c = *static_cast<Context*>(p); c.self->run_actor(c.actor);
    }
    explicit Rig(Scenario input) : s(input), origin(s.now) {
        permissions = {WorkOpportunity(s.where), WorkProfile(s.profile), bool(s.root_opt), true, true, true};
        graph.actors[1].producer = WaitProducer(s.root_kind); graph.actors[1].opted = s.root_opt;
        graph.actors[2].producer = WaitProducer(s.helper_kind); graph.actors[2].opted = s.helper_opt;
        for (unsigned i = 0; i < 3; ++i) {
            graph.owners[i] = int8_t(s.owner[i]);
            graph.actors[i + 1].needs = s.needs[i]; graph.actors[i + 1].waits_for = s.deps[i];
        }
        for (unsigned i = 0; i < 4; ++i) {
            graph.actors[i].known = (s.known & (1u << i)) != 0;
            contexts[i] = {this, i};
        }
        for (unsigned i : {2u, 3u, 1u}) {
            auto& actor = graph.actors[i];
            const auto domain = actor.producer == WaitProducer::User ? WorkDomain::User : WorkDomain::System;
            if constexpr (Variant == 1) this->token[i] = this->executor.add(work, &contexts[i], domain, actor.opted);
            if constexpr (Variant == 2) this->token[i] = this->executor.add(event, &contexts[i], domain, actor.opted);
            post(i);
        }
        if constexpr (Variant != 0) this->executor.initialize();
        if (s.preloaded) assert(result.push(17));
    }
    bool normal(unsigned i) const {
        const auto kind = graph.actors[i].producer;
        return kind == WaitProducer::System || kind == WaitProducer::User;
    }
    bool allowed(unsigned i) const {
        auto p = permissions; p.user_opted = graph.actors[i].opted;
        return p.supports(graph.actors[i].producer);
    }
    void post(unsigned i) {
        if (!normal(i)) return;
        if constexpr (Variant == 1) assert(this->executor.post(this->token[i]));
        if constexpr (Variant == 2) assert(this->executor.notify(this->token[i], 1));
    }
    void run_actor(unsigned i) {
        if (done & (1u << i)) return;
        if (normal(i)) ++normal_calls;
        auto& actor = graph.actors[i];
        bool ready = !(actor.waits_for & ~done) && (i != 1 || uint32_t(s.now - origin) >= s.due);
        for (unsigned r = 0; r < 3; ++r)
            if ((actor.needs & (1u << r)) && graph.owners[r] != -1) ready = false;
        if (!ready) { post(i); return; }
        // A finite all-or-none try acquisition in this serial rig. This is not
        // an implementation of a hardware bus lock or an IRQ-safe pool.
        for (unsigned r = 0; r < 3; ++r) if (actor.needs & (1u << r)) graph.owners[r] = int8_t(i);
        if (i == 1) { assert(result.push(17)); ++produced; }
        done |= 1u << i;
        for (auto& other : graph.actors) other.waits_for &= ~(1u << i);
        for (auto& owner : graph.owners) if (owner == int(i)) owner = -1;
    }
    bool drive() {
        ++drives; ++s.now;
        const uint32_t elapsed = s.now - origin;
        if (s.mode >= 2 && s.release && elapsed == s.release) for (auto& owner : graph.owners) if (owner == 0) owner = -1;
        if (s.inject && elapsed == s.inject) graph.owners[0] = 0;
        for (unsigned i : {2u, 3u, 1u}) if (!normal(i) && allowed(i)) run_actor(i);
        if constexpr (Variant == 0) {
            for (unsigned i : {2u, 3u, 1u}) if (normal(i) && allowed(i)) run_actor(i);
        } else this->executor.turn(3, WorkOpportunity(s.where), WorkProfile(s.profile));
        return true;
    }
};
void contracts() {
    WaitPermissions p; p.clock_running = true;
    WaitDependencies<4, 3> g;
    g.actors[1].waits_for = (1u << 2) | (1u << 3); g.actors[2].waits_for = 1u << 3;
    assert(g.inspect(1, 0, p) == DependencyCheck::NoKnownBlock); // diamond, no cycle
    g.actors[3].waits_for = 1u << 2;
    assert(g.inspect(1, 0, p) == DependencyCheck::Cycle);
    g.actors[1].waits_for = 0;
    assert(g.inspect(1, 0, p) == DependencyCheck::NoKnownBlock); // disconnected cycle
    g.actors[1].needs = 1; g.owners[0] = 1;
    assert(g.inspect(1, 0, p) == DependencyCheck::Cycle); // nonrecursive reacquisition
    g.owners[0] = 0; assert(g.inspect(1, 0, p) == DependencyCheck::HeldByWaiter);
    g.owners[0] = -2; assert(g.inspect(1, 0, p) == DependencyCheck::Unknown);
    g.owners[0] = 12; assert(g.inspect(1, 0, p) == DependencyCheck::Invalid);
    assert(g.inspect(4, 0, p) == DependencyCheck::Invalid);
    assert(g.inspect(1, 4, p) == DependencyCheck::Invalid);
    g.owners[0] = -1; g.actors[1].needs = 8;
    assert(g.inspect(1, 0, p) == DependencyCheck::Invalid);
    g.actors[1].needs = 0; g.actors[1].waits_for = 16;
    assert(g.inspect(1, 0, p) == DependencyCheck::Invalid);
    WaitDependencies<16, 16> wide;
    wide.actors[1].needs = 1u << 15; wide.owners[15] = 15;
    wide.actors[15].waits_for = 1;
    assert(wide.inspect(1, 0, p) == DependencyCheck::HeldByWaiter);
    assert(wide.inspect(1, 1, p) == DependencyCheck::HeldByWaiter);
}
template<unsigned Variant> void run(Scenario s) {
    Rig<Variant> rig(s); uint32_t value = 99;
    auto attempt = [&] { return rig.result.pop(value); };
    auto drive = [&] { return rig.drive(); };
    DependencyDrive<decltype(rig.graph), decltype(drive)> guard{rig.graph, rig.permissions, 1, 0, drive};
    WaitOutcome outcome{WaitStatus::Pending, 0};
    if (s.mode <= 1) {
        auto selected_drive = [&] { return s.mode == 1 ? guard() : drive(); };
        outcome = wait_for(attempt, selected_drive, [&] { return rig.s.now; }, s.timeout,
                           WorkContext(s.context), WaitProducer(s.root_kind), rig.permissions);
    } else {
        AsyncWait waiting;
        if (!waiting.arm(s.now, s.timeout)) outcome.status = WaitStatus::InvalidTimeout;
        else {
            outcome = waiting.poll(rig.s.now, attempt, WorkContext(s.context));
            for (unsigned i = 0; i < 32 && outcome.status == WaitStatus::Pending; ++i) {
                drive(); outcome = waiting.poll(rig.s.now, attempt, WorkContext(s.context));
            }
        }
    }
    std::cout << unsigned(outcome.status) << ' ' << outcome.elapsed << ' ' << rig.drives << ' '
              << rig.normal_calls << ' ' << rig.produced << ' ' << rig.result.size() << ' ' << rig.done;
    for (auto owner : rig.graph.owners) std::cout << ' ' << int(owner);
    std::cout << ' ' << unsigned(guard.last) << ' ' << guard.checks << ' ' << value << '\n';
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "contracts") { contracts(); return 0; }
    Scenario s{};
    while (std::cin >> s.candidate >> s.mode >> s.where >> s.profile >> s.root_kind >> s.root_opt >> s.helper_kind >> s.helper_opt
           >> s.owner[0] >> s.owner[1] >> s.owner[2] >> s.needs[0] >> s.needs[1] >> s.needs[2]
           >> s.deps[0] >> s.deps[1] >> s.deps[2] >> s.known >> s.timeout >> s.due >> s.release >> s.inject
           >> s.context >> s.preloaded >> s.now) {
        if (s.candidate == 0) run<0>(s);
        else if (s.candidate == 1) run<1>(s);
        else { assert(s.candidate == 2); run<2>(s); }
    }
}
