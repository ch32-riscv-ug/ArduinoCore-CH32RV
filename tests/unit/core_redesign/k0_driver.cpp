#include "k0_candidates.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace experiment;
template<class App> void protocol(WorkDomain domain, bool opt) {
    App app;
    TurnStatus status = TurnStatus::Ran;
    std::string command;
    while (std::cin >> command) {
        if (command == "begin") app.begin(domain, opt);
        else if (command == "step") status = app.opportunity(WorkOpportunity::Explicit, WorkProfile::M);
        else if (command == "opportunity") {
            unsigned where, profile, context; std::cin >> where >> profile >> context;
            status = app.opportunity(WorkOpportunity(where), WorkProfile(profile), WorkContext(context));
        } else if (command == "input") { int value; std::cin >> value; app.input(int16_t(value)); }
        else if (command == "begin_write") app.begin_write();
        else if (command == "byte") { unsigned value; std::cin >> value; app.byte(uint8_t(value)); }
        else if (command == "stop") app.stop();
        else if (command == "restart") app.restart();
        else if (command == "begin_read") app.begin_read();
        else { assert(command == "read"); app.read(); }
        // This is the explicit connection from the serialized frontend to
        // deferred progress; no handler is called inline by a frontend.
        app.changed();
        std::cout << app.position << ' ' << app.increment << ' ' << app.lower << ' ' << app.upper << ' '
                  << app.sequence << ' ' << unsigned(app.status) << ' ' << app.delta << ' '
                  << app.inbox.size() << ' ' << app.reading << ' ' << unsigned(app.read_index) << ' '
                  << app.reply << ' ' << app.accepted << ' ' << unsigned(status) << '\n';
    }
}
template<unsigned N> void select(const std::string& candidate, WorkDomain domain, bool opt) {
    if (candidate == "R") protocol<K0R<N>>(domain, opt);
    else if (candidate == "A") protocol<K0A<N>>(domain, opt);
    else if (candidate == "B") protocol<K0B<N>>(domain, opt);
    else { assert(candidate == "C"); protocol<K0C<N>>(domain, opt); }
}
int main(int argc, char** argv) {
    assert(argc == 5);
    const auto n = std::strtoul(argv[2], nullptr, 10);
    const auto domain = WorkDomain(std::strtoul(argv[3], nullptr, 10));
    const bool opt = std::strtoul(argv[4], nullptr, 10) != 0;
    if (n == 1) select<1>(argv[1], domain, opt);
    else { assert(n == 2); select<2>(argv[1], domain, opt); }
}
