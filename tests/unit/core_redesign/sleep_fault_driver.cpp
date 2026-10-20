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

// Compose preparation/abort failure with live leases. No fake clock sleep or
// completion is hidden in retry: all terminals and takes below are explicit.
template<unsigned Capacity, bool Timed>
int run_lease(unsigned kind, unsigned fault_side, unsigned lease_side, bool both,
              bool reverse, unsigned outcome) {
    using List = typename std::conditional<Capacity == 0, Explicit<FaultPort, FaultPort>, Registry<Capacity ? Capacity : 1>>::type;
    using Deadlines = typename std::conditional<Timed, Timer, NoDeadline>::type;
    List list;
    Clock clock;
    Deadlines timer;
    Coordinator<List, uint32_t, Deadlines> flow;
    FaultPort ports[2];
    experiment::IoSlot<32>::Completion collected[2];
    time_experiment::Stamp stamp{};
    uint8_t readback[2] = {222, 222};
    assert(time_experiment::ready(clock.begin({0, 1, 6000000}, 0, stamp)));
    for (auto& p : ports) { assert(p.port.init()); p.source = &clock; }
    if constexpr (Capacity == 0) assert(list.bind(ports[0], ports[1]));
    else {
        typename List::Token token{};
        for (auto& p : ports) assert(list.add(&p, &FaultPort::callback, token));
    }
    if constexpr (Timed) assert(flow.bind(list, clock, timer));
    else assert(flow.bind(list, clock));
    bool leased[2] = {both || lease_side == 0, both || lease_side == 1};
    for (unsigned i = 0; i < 2; ++i)
        if (leased[i]) assert(ports[i].port.start(4) == experiment::IoStart::Accepted);
    auto& injected = ports[fault_side];
    injected.action = kind == 0 ? Action::Prepare : Action::Abort;
    injected.injected = kind == 2 ? Reply::Waiting : Reply::Failed;
    injected.armed = true;
    uint32_t attempt = 0;
    auto snapshot = [&](const char* label) {
        std::cout << "{\"label\":\"" << label << "\",\"phase\":" << unsigned(flow.phase())
                  << ",\"attempt\":" << flow.attempt() << ",\"clock_phase\":" << unsigned(clock.phase())
                  << ",\"frozen\":" << list.frozen() << ",\"input\":" << flow.input_pending() << ",\"ports\":[";
        for (unsigned i = 0; i < 2; ++i) {
            auto& p = ports[i].port;
            if (i) std::cout << ',';
            std::cout << "{\"powered\":" << p.powered << ",\"admitting\":" << p.admitting << ",\"active\":" << p.io.active()
                      << ",\"terminal\":" << p.io.terminal() << ",\"cancelled\":" << p.io.cancel_pending()
                      << ",\"available\":" << p.buffer.available() << ",\"ticket\":" << p.io.token()
                      << ",\"result\":" << unsigned(collected[i].result) << ",\"count\":" << collected[i].transferred
                      << ",\"read\":" << unsigned(readback[i]) << '}';
        }
        std::cout << "]}\n";
    };
    auto retry_once = [&]() {
        assert(flow.phase() == Phase::Fault && flow.retry());
        flow.poll();
    };
    assert(flow.request(attempt));
    if (kind != 0) { flow.post_input(); flow.poll(); }
    assert(flow.phase() == Phase::Fault);
    snapshot("injected-with-live-lease");
    injected.armed = false;
    retry_once();
    assert(flow.phase() == Phase::Fault);
    snapshot("retry-before-stop");
    for (unsigned i = 0; i < 2; ++i) {
        auto& p = ports[i].port;
        assert(p.start(4) == experiment::IoStart::Closed);
        if (!leased[i]) continue;
        assert(p.io.active() && !p.buffer.available());
        assert(!p.finish(1, experiment::IoResult::Cancelled, 2, 77, false));
        assert(!p.finish(2, experiment::IoResult::Cancelled, 2, 77, true));
        assert(!p.io.take(2, collected[i]));
        assert(!p.buffer.write(0, 99) && !p.buffer.read(0, readback[i]) && readback[i] == 222);
    }
    snapshot("invalid-stop-and-stale-take");
    const unsigned first = both ? unsigned(reverse) : lease_side;
    const unsigned count = outcome == unsigned(experiment::IoResult::Success) ? 4 : 2;
    auto finish_and_take = [&](unsigned i, bool last) {
        auto& p = ports[i].port;
        const uint8_t fill = uint8_t(77 + 53*i);
        assert(p.finish(1, experiment::IoResult(outcome), count, fill, true));
        assert(!p.buffer.available() && !p.buffer.read(0, readback[i]));
        retry_once();
        assert(flow.phase() == Phase::Fault);
        snapshot(last ? "last-terminal-not-collected" : "first-terminal-not-collected");
        assert(p.io.take(1, collected[i]));
        assert(p.buffer.read(0, readback[i]) && readback[i] == fill);
        assert(p.start(4) == experiment::IoStart::Closed);
        snapshot(last ? "last-collected-before-retry" : "first-collected-before-retry");
        retry_once();
        assert(flow.phase() == (last ? Phase::Active : Phase::Fault));
        snapshot(last ? "repaired" : "one-lease-still-held");
    };
    finish_and_take(first, !both);
    if (both) finish_and_take(first ^ 1, true);
    assert(flow.take_input() == (kind != 0));
    for (unsigned i = 0; i < 2; ++i) if (leased[i]) {
        auto& p = ports[i].port;
        assert(!p.finish(1, experiment::IoResult::Success, 4, 99, true));
        assert(!p.io.take(1, collected[i]));
    }
    auto& reused = ports[first].port;
    assert(reused.start(4) == experiment::IoStart::Accepted && reused.io.token() == 2);
    assert(!reused.finish(1, experiment::IoResult::Success, 4, 99, true));
    assert(!reused.io.take(1, collected[first]) && !reused.buffer.available());
    snapshot("old-result-cannot-release-new-lease");
    assert(reused.finish(2, experiment::IoResult::Success, 4, 99, true));
    assert(reused.io.take(2, collected[first]));
    snapshot("final-collected");
    assert(flow.phase() == Phase::Active && !list.frozen() && clock.phase() == time_experiment::State::Running);
    for (const auto& p : ports) assert(!p.port.io.active() && p.port.buffer.available() && p.port.admitting);
    return 0;
}
int main(int argc, char** argv) {
    if (argc == 10 && std::string(argv[1]) == "lease") {
        const unsigned variant = unsigned(std::stoul(argv[2])), kind = unsigned(std::stoul(argv[4]));
        const unsigned fault_side = unsigned(std::stoul(argv[5])), lease_side = unsigned(std::stoul(argv[6]));
        const bool timed = std::string(argv[3]) == "1", both = std::string(argv[7]) == "1", reverse = std::string(argv[8]) == "1";
        const unsigned outcome = unsigned(std::stoul(argv[9]));
        if (kind > 2 || fault_side > 1 || lease_side > 1 || outcome < 1 || outcome > 3) return 2;
        switch (variant) {
        case 0: return timed ? run_lease<0, true>(kind, fault_side, lease_side, both, reverse, outcome) : run_lease<0, false>(kind, fault_side, lease_side, both, reverse, outcome);
        case 2: return timed ? run_lease<2, true>(kind, fault_side, lease_side, both, reverse, outcome) : run_lease<2, false>(kind, fault_side, lease_side, both, reverse, outcome);
        case 4: return timed ? run_lease<4, true>(kind, fault_side, lease_side, both, reverse, outcome) : run_lease<4, false>(kind, fault_side, lease_side, both, reverse, outcome);
        default: return 2;
        }
    }
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
