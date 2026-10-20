#include "sleep_coordinator.hpp"
#include <cassert>
#include <iostream>
#include <string>
using namespace sleep_experiment;

struct FaultPort {
    Port<> port;
    Clock* source = nullptr;
    Action action = Action::Quiet;
    Reply injected = Reply::Failed;
    bool armed = false, lose_clock = false;
    unsigned calls[6]{};
    Reply step(Action a) {
        ++calls[unsigned(a)];
        const Reply reply = port.step(a);
        if (armed && a == action) {
            if (lose_clock) source->pause();
            else return injected;
        }
        return reply;
    }
    static Reply callback(void* context, Action action) { return static_cast<FaultPort*>(context)->step(action); }
};

template<unsigned Capacity, bool Timed> int run(unsigned kind, unsigned side, unsigned injected) {
    using List = typename std::conditional<Capacity == 0, Explicit<FaultPort, FaultPort>, Registry<Capacity ? Capacity : 1>>::type;
    using Deadlines = typename std::conditional<Timed, Timer, NoDeadline>::type;
    List list;
    Clock clock;
    Deadlines timer;
    Coordinator<List, uint32_t, Deadlines> flow;
    FaultPort ports[2];
    time_experiment::Stamp stamp{};
    assert(time_experiment::ready(clock.begin({0, 1, 6000000}, 0, stamp)));
    for (auto& p : ports) { assert(p.port.init()); p.source = &clock; }
    if constexpr (Capacity == 0) assert(list.bind(ports[0], ports[1]));
    else {
        typename List::Token token{};
        for (auto& p : ports) assert(list.add(&p, &FaultPort::callback, token));
    }
    if constexpr (Timed) assert(flow.bind(list, clock, timer));
    else assert(flow.bind(list, clock));
    const Action targets[] = {Action::Prepare, Action::Quiet, Action::Off, Action::Restore, Action::Activate, Action::Abort, Action::Quiet};
    ports[side].action = targets[kind];
    ports[side].injected = Reply(injected);
    ports[side].lose_clock = kind == 6;
    ports[side].armed = true;
    uint32_t attempt = 0;
    auto snapshot = [&](const char* label) {
        std::cout << "{\"label\":\"" << label << "\",\"phase\":" << unsigned(flow.phase())
                  << ",\"clock_phase\":" << unsigned(clock.phase()) << ",\"clock_ready\":" << flow.clock_ready()
                  << ",\"frozen\":" << list.frozen() << ",\"input\":" << flow.input_pending() << ",\"ports\":[";
        for (unsigned i = 0; i < 2; ++i) {
            if (i) std::cout << ',';
            auto& p = ports[i];
            std::cout << '[' << p.port.powered << ',' << p.port.admitting << ',' << p.port.io.opened() << ',' << p.port.io.active() << ",[";
            for (unsigned j = 0; j < 6; ++j) { if (j) std::cout << ','; std::cout << p.calls[j]; }
            std::cout << "]]";
        }
        std::cout << "]}\n";
    };
    assert(flow.request(attempt));
    if (kind != 0) {
        assert(flow.poll() == Phase::Armed);
        if (kind == 5) { flow.post_input(); flow.poll(); }
        else {
            const Entry entry = flow.enter(attempt, true, true, Injection::None);
            if (kind == 3 || kind == 4) {
                assert(entry == Entry::Sleeping && flow.wake(attempt));
                assert(time_experiment::ready(flow.restore_clock(attempt, {0, 2, 6000000}, false, 0, 0, stamp)));
                for (auto& p : ports) p.port.restoration = Reply::Ready;
                flow.poll();
            }
        }
    } else flow.poll();
    snapshot("injected");
    for (auto& p : ports) p.armed = false;
    if (kind == 6) {
        assert(flow.retry());
        flow.poll();
        snapshot("retry-with-paused-clock");
        assert(time_experiment::ready(flow.restore_clock(attempt, {0, 2, 6000000}, false, 0, 0, stamp)));
    } else if (kind == 2) {
        assert(time_experiment::ready(flow.restore_clock(attempt, {0, 2, 6000000}, false, 0, 0, stamp)));
        for (auto& p : ports) p.port.restoration = Reply::Ready;
    }
    if (flow.phase() == Phase::Fault) assert(flow.retry());
    if (flow.phase() == Phase::Quiescing) {
        assert(flow.poll() == Phase::Armed);
        assert(flow.abort());
    } else flow.poll();
    snapshot("repaired");
    assert(flow.phase() == Phase::Active && flow.clock_ready() && !list.frozen());
    for (const auto& p : ports) assert(p.port.powered && p.port.admitting && !p.port.io.active());
    return 0;
}
int main(int argc, char** argv) {
    if (argc != 6) return 2;
    const unsigned variant = unsigned(std::stoul(argv[1])), kind = unsigned(std::stoul(argv[3]));
    const unsigned side = unsigned(std::stoul(argv[4])), injected = unsigned(std::stoul(argv[5]));
    if (kind > 6 || side > 1 || (injected != 0 && injected != 2)) return 2;
    const bool timed = std::string(argv[2]) == "1";
    switch (variant) {
    case 0: return timed ? run<0, true>(kind, side, injected) : run<0, false>(kind, side, injected);
    case 2: return timed ? run<2, true>(kind, side, injected) : run<2, false>(kind, side, injected);
    case 4: return timed ? run<4, true>(kind, side, injected) : run<4, false>(kind, side, injected);
    default: return 2;
    }
}
