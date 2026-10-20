#include "deadline_timer.hpp"
#include <cassert>
#include <iostream>
#include <string>

int main() {
    experiment::DeadlineTimer timer;
    experiment::DeadlineTimer::Expiration last;
    uint32_t now = 0xfffffff0u;
    std::string command;
    while (std::cin >> command) {
        bool result = false;
        if (command == "arm") { uint32_t delay, period; std::cin >> delay >> period; result = timer.arm(now, delay, period); }
        else if (command == "advance") { uint32_t delta; std::cin >> delta; now += delta; result = true; }
        else if (command == "poll") result = timer.update(now);
        else if (command == "take") result = timer.take(last);
        else { assert(command == "stop"); result = timer.stop(); }
        std::cout << result << ' ' << now << ' ' << timer.armed() << ' ' << timer.ready() << ' '
                  << timer.deadline() << ' ' << timer.period() << ' ' << last.latest_due << ' '
                  << last.skipped << ' ' << last.saturated << '\n';
    }
}
