#include "sleep_coordinator.hpp"
#include <iostream>
#include <sstream>
#include <string>
using namespace sleep_experiment;

template<unsigned Capacity, class G, bool Timed = true> int run() {
    using List = typename std::conditional<Capacity == 0, Explicit<Port<>, Port<>>, Registry<Capacity ? Capacity : 1, G>>::type;
    using Deadlines = typename std::conditional<Timed, Timer, NoDeadline>::type;
    using Flow = Coordinator<List, G, Deadlines>;
    List list;
    Flow flow;
    Clock clock;
    Deadlines timer;
    Port<> ports[5];
    G attempt = 0;
    unsigned reg_generation = 0, reg_index = 99;
    time_experiment::Stamp stamp{};
    time_experiment::Notice notice{};
    auto clock_status = time_experiment::Result::Closed;
    auto source_status = [&]() { return clock.phase() == time_experiment::State::Paused ? time_experiment::Result::Paused : clock_status; };
    experiment::IoSlot<32>::Completion completed[5];
    uint8_t output = 222;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string op;
        uint64_t a[8]{};
        in >> op;
        for (auto& n : a) in >> n;
        unsigned i = unsigned(a[0]);
        int status = 0;
        if (op == "init") {
            if (flow.phase() == Phase::Unbound) {
                clock_status = clock.begin({0, 1, 6000000}, 0, stamp);
                bool ok = time_experiment::ready(clock_status);
                for (auto& port : ports) ok = port.init() && ok;
                if constexpr (Capacity == 0) ok = list.bind(ports[0], ports[1]) && ok;
                else {
                    typename List::Token token{};
                    ok = list.add(&ports[0], &Port<>::callback, token) && ok;
                    ok = list.add(&ports[1], &Port<>::callback, token) && ok;
                    reg_generation = token.generation; reg_index = token.index;
                }
                if constexpr (Timed) status = ok && flow.bind(list, clock, timer);
                else status = ok && flow.bind(list, clock);
            }
        } else if (op == "start" && i < 5) status = int(ports[i].start(unsigned(a[1])));
        else if (op == "finish" && i < 5)
            status = ports[i].finish(uint32_t(a[1]), experiment::IoResult(a[2]), unsigned(a[3]), uint8_t(a[4]), a[5] != 0);
        else if (op == "take" && i < 5) status = ports[i].io.take(uint32_t(a[1]), completed[i]);
        else if (op == "read" && i < 5) status = ports[i].buffer.read(unsigned(a[1]), output);
        else if (op == "ready" && i < 5 && a[1] <= 2) { ports[i].restoration = Reply(a[1]); status = 1; }
        else if (op == "observe") {
            clock_status = clock.observe({a[1], uint32_t(a[0]), 6000000}, {a[2], true}, stamp); status = int(clock_status);
        } else if (op == "arm") {
            if constexpr (Timed) status = timer.arm(source_status(), stamp, uint32_t(a[0]), uint32_t(a[1]));
            else status = -1;
        } else if (op == "timer_poll") {
            if constexpr (Timed) status = int(timer.poll(source_status(), stamp));
            else status = -1;
        } else if (op == "timer_take") {
            if constexpr (Timed) status = timer.take(notice);
            else status = -1;
        }
        else if (op == "request") status = flow.request(attempt);
        else if (op == "poll") status = int(flow.poll());
        else if (op == "post") flow.post_input();
        else if (op == "input_take") status = flow.take_input();
        else if (op == "abort") status = flow.abort();
        else if (op == "enter") status = int(flow.enter(G(a[0]), a[1] != 0, a[2] != 0, Injection(a[3])));
        else if (op == "wake") status = flow.wake(G(a[0]));
        else if (op == "restore") {
            clock_status = flow.restore_clock(G(a[0]), {a[2], uint32_t(a[1]), 6000000}, a[3] != 0, a[4], a[5], stamp);
            status = int(clock_status);
        } else if (op == "retry") status = flow.retry();
        else if (op == "add" && i < 5) {
            if constexpr (Capacity > 0) {
                typename List::Token token{G(reg_generation), uint8_t(reg_index)};
                status = list.add(&ports[i], &Port<>::callback, token);
                reg_generation = token.generation; reg_index = token.index;
            }
        } else if (op == "remove") {
            if constexpr (Capacity > 0) status = list.remove({G(a[0]), uint8_t(a[1])});
        } else return 2;
        std::cout << "{\"status\":" << status << ",\"phase\":" << unsigned(flow.phase())
                  << ",\"attempt\":" << unsigned(flow.attempt()) << ",\"out_attempt\":" << unsigned(attempt)
                  << ",\"input\":" << flow.input_pending() << ",\"clock_ready\":" << flow.clock_ready()
                  << ",\"frozen\":" << list.frozen() << ",\"count\":" << list.count()
                  << ",\"registration\":[" << reg_generation << ',' << reg_index << ']'
                  << ",\"clock_status\":" << unsigned(clock_status)
                  << ",\"clock\":[" << unsigned(clock.phase()) << ',' << clock.history_us() << ',' << clock.epoch() << ',' << clock.source()
                  << "],\"stamp\":[" << stamp.us << ',' << stamp.epoch << ',' << unsigned(stamp.quality)
                  << "],\"timer_enabled\":" << int(Timed) << ",\"timer\":[";
        if constexpr (Timed) std::cout << timer.armed() << ',' << timer.pending() << ',' << timer.epoch();
        else std::cout << "0,0,0";
        std::cout << "],\"notice\":[" << notice.expiration.latest_due << ',' << notice.expiration.skipped << ',' << notice.expiration.saturated << ',' << notice.epoch
                  << "],\"read\":" << unsigned(output) << ",\"registered\":[";
        for (unsigned n = 0; n < 5; ++n) {
            if (n) std::cout << ',';
            std::cout << list.contains(&ports[n]);
        }
        std::cout << "],\"ports\":[";
        for (unsigned n = 0; n < 5; ++n) {
            auto& p = ports[n]; auto& c = completed[n];
            if (n) std::cout << ',';
            std::cout << '[' << p.powered << ',' << p.admitting << ',' << p.io.opened() << ',' << p.io.active()
                      << ',' << p.io.terminal() << ',' << p.io.cancel_pending() << ',' << p.io.notification_pending()
                      << ',' << p.buffer.available() << ',' << p.io.token() << ',' << unsigned(p.io.result())
                      << ',' << unsigned(c.result) << ',' << c.transferred << ',' << c.digest << ']';
        }
        std::cout << "]}\n";
    }
    return 0;
}
int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) return 2;
    const bool small = std::string(argv[2]) == "short";
    const bool timed = argc == 3 || std::string(argv[3]) != "notimer";
    switch (std::stoi(argv[1])) {
    case 0: return timed ? (small ? run<0, uint8_t>() : run<0, uint32_t>()) : (small ? run<0, uint8_t, false>() : run<0, uint32_t, false>());
    case 2: return timed ? (small ? run<2, uint8_t>() : run<2, uint32_t>()) : (small ? run<2, uint8_t, false>() : run<2, uint32_t, false>());
    case 4: return timed ? (small ? run<4, uint8_t>() : run<4, uint32_t>()) : (small ? run<4, uint8_t, false>() : run<4, uint32_t, false>());
    default: return 2;
    }
}
