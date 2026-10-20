#include "wait_policy.hpp"
#include "event_executor.hpp"
#include <cassert>
#include <iostream>
#include <string>

using namespace experiment;
template<unsigned Variant> struct Adapter {};
template<> struct Adapter<1> { WorkExecutor<1> executor; WorkExecutor<1>::Token token; };
template<> struct Adapter<2> { EventExecutor<uint32_t, 1, 1> executor; EventExecutor<uint32_t, 1, 1>::Token token; };
struct Scenario {
    unsigned candidate, producer, where, profile, opted, irq, remote, clock, context;
    uint32_t timeout, due, jump, now;
    bool preloaded, stop, nested;
};
template<unsigned Variant> struct Rig : Adapter<Variant> {
    Scenario s;
    Queue<uint32_t, 1> queue;
    uint32_t origin;
    unsigned drives = 0, callbacks = 0;
    bool posted = false, produced = false, ready = false;
    WaitStatus nested_status = WaitStatus::Idle;
    WaitPermissions permissions;
    static void work(void* p, WorkExecutor<1>::Token) { static_cast<Rig*>(p)->produce(); }
    static void event(void* p, const EventExecutor<uint32_t, 1, 1>::Delivery&) { static_cast<Rig*>(p)->produce(); }
    explicit Rig(Scenario scenario) : s(scenario), origin(s.now) {
        permissions = {WorkOpportunity(s.where), WorkProfile(s.profile), bool(s.opted), bool(s.irq), bool(s.remote), bool(s.clock)};
        if (s.preloaded) assert(queue.push(17));
        const auto domain = s.producer == 2 ? WorkDomain::User : WorkDomain::System;
        if constexpr (Variant == 1) {
            this->token = this->executor.add(work, this, domain, s.opted); this->executor.initialize();
        } else if constexpr (Variant == 2) {
            this->token = this->executor.add(event, this, domain, s.opted); this->executor.initialize();
        }
    }
    void produce() {
        ++callbacks; assert(queue.push(17)); produced = true;
        if (s.nested) {
            uint32_t value = 99; unsigned driven = 0;
            auto outcome = wait_for([&] { return queue.pop(value); }, [&] { ++driven; return true; },
                                    [&] { return s.now; }, 9, WorkContext::Callback, WaitProducer::System, permissions);
            nested_status = outcome.status;
            assert(nested_status == WaitStatus::Forbidden && value == 99 && !driven && queue.size() == 1);
        }
    }
    bool drive() {
        ++drives;
        if (s.stop) return false;
        if (s.clock) s.now += s.jump;
        if (!posted && uint32_t(s.now - origin) >= s.due) {
            posted = true;
            if (s.producer == 0 || s.producer == 3) {
                if ((s.producer == 0 && s.irq) || (s.producer == 3 && s.remote)) {
                    assert(queue.push(17)); produced = true;
                }
            } else {
                if constexpr (Variant == 0) ready = true;
                else if constexpr (Variant == 1) assert(this->executor.post(this->token));
                else assert(this->executor.notify(this->token, 1));
            }
        }
        if constexpr (Variant == 0) {
            if (ready && permissions.supports(WaitProducer(s.producer))) { ready = false; produce(); }
        } else this->executor.turn(1, WorkOpportunity(s.where), WorkProfile(s.profile));
        return true;
    }
};
void contracts() {
    Queue<uint32_t, 1> queue;
    AsyncWait wait;
    uint32_t value = 99; unsigned attempts = 0;
    auto attempt = [&] { ++attempts; return queue.pop(value); };
    assert(wait.arm(0xfffffffcu, 8));
    assert(!wait.arm(0, 0x80000000u)); // Invalid replacement leaves the old wait intact.
    auto r = wait.poll(0xfffffffdu, attempt, WorkContext::Callback);
    assert(r.status == WaitStatus::Pending && r.elapsed == 1 && attempts == 1 && value == 99);
    assert(queue.push(17));
    r = wait.poll(0xfffffffeu, attempt, WorkContext::Isr);
    assert(r.status == WaitStatus::Forbidden && attempts == 1 && queue.size() == 1);
    r = wait.poll(0, attempt, WorkContext::Callback);
    assert(r.status == WaitStatus::Completed && r.elapsed == 4 && value == 17);
    assert(queue.push(18));
    assert(wait.poll(1, attempt).status == WaitStatus::Idle && queue.size() == 1 && attempts == 2);
    assert(wait.arm(1, 8) && wait.cancel());
    assert(!wait.cancel() && wait.poll(2, attempt).status == WaitStatus::Idle && queue.size() == 1);
}
template<unsigned Variant> void run(const std::string& mode, Scenario s) {
    Rig<Variant> rig(s); uint32_t value = 99;
    auto attempt = [&] { return rig.queue.pop(value); };
    WaitOutcome result{WaitStatus::Pending, 0};
    if (mode == "try") result.status = attempt() ? WaitStatus::Completed : WaitStatus::Pending;
    else if (mode == "blocking") result = wait_for(attempt, [&] { return rig.drive(); }, [&] { return rig.s.now; },
                                                   s.timeout, WorkContext(s.context), WaitProducer(s.producer), rig.permissions);
    else {
        assert(mode == "async"); AsyncWait wait;
        if (!wait.arm(s.now, s.timeout)) result.status = WaitStatus::InvalidTimeout;
        else for (unsigned i = 0; i <= 16; ++i) {
            result = wait.poll(rig.s.now, attempt, WorkContext(s.context));
            if (result.status != WaitStatus::Pending || i == 16 || !rig.drive()) break;
        }
    }
    std::cout << unsigned(result.status) << ' ' << result.elapsed << ' ' << rig.drives << ' ' << rig.callbacks
              << ' ' << value << ' ' << rig.queue.size() << ' ' << rig.produced << ' ' << rig.s.now
              << ' ' << unsigned(rig.nested_status) << '\n';
}
int main(int argc, char** argv) {
    assert(argc == 2); const std::string mode = argv[1];
    if (mode == "contracts") { contracts(); return 0; }
    Scenario s;
    while (std::cin >> s.candidate >> s.producer >> s.where >> s.profile >> s.opted >> s.irq >> s.remote >> s.clock >> s.context
                    >> s.timeout >> s.due >> s.jump >> s.now >> s.preloaded >> s.stop >> s.nested) {
        if (s.candidate == 0) run<0>(mode, s);
        else if (s.candidate == 1) run<1>(mode, s);
        else { assert(s.candidate == 2); run<2>(mode, s); }
    }
}
