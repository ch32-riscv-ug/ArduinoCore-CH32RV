#include "h417_features.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>
using namespace h417_feature_experiment;
struct Gate {
    bool blocked = false, held = false;
    bool try_enter() { if (blocked || held) return false; held = true; return true; }
    void leave() { held = false; }
};
struct Observer {
    bool first = true;
    void start(unsigned d, unsigned e, unsigned g, bool r, unsigned q, unsigned n, unsigned m, unsigned b) {
        std::cout << ",\"state\":[" << d << ',' << e << ',' << g << ',' << r << ',' << q << ',' << n << ',' << m << ',' << b << "],\"messages\":[";
    }
    void message(const Message& m) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << '[' << m.words[0] << ',' << m.words[1] << ',' << m.words[2] << ',' << m.words[3] << ']';
    }
    void blocks() { std::cout << "],\"blocks\":["; first = true; }
    void block(unsigned p, unsigned o, unsigned g, unsigned w, uint32_t h) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << '[' << p << ',' << o << ',' << g << ',' << w << ',' << h << ']';
    }
    void end() { std::cout << "]}\n"; }
};
template<unsigned Features, Owners P, class Counter> int run(unsigned domain) {
    Gate gate;
    Fabric<P, Gate, (Features & 1) != 0, (Features & 2) != 0, Counter> fabric(gate, domain);
    Token output{99, 99, 99, 99};
    Message message{{99, 99, 99, 99}};
    unsigned byte = 999;
    for (std::string line; std::getline(std::cin, line);) {
        std::istringstream in(line);
        std::string op; unsigned core = 0; bool ok = false;
        in >> op >> core;
        Token t{}; unsigned index = 0, value = 0;
        if (op == "push") {
            Message m{}; for (auto& w : m.words) in >> w;
            ok = fabric.push(core, m);
        } else if (op == "pop") ok = fabric.pop(core, message);
        else if (op == "reserve") ok = fabric.reserve(core, output);
        else if (op == "take") ok = fabric.take(core, output);
        else if (op == "ack") { in >> value; ok = fabric.ack(core, value); }
        else if (op == "recover") ok = fabric.begin_recovery(core);
        else if (op == "attest") { in >> value; ok = fabric.attest(core, value); }
        else if (op == "restart") ok = fabric.restart(core);
        else if (op == "gate") { gate.blocked = core != 0; ok = true; }
        else if (op == "snapshot") ok = true;
        else {
            in >> t.domain >> t.epoch >> t.generation >> t.index;
            if (op == "write") { in >> index >> value; ok = fabric.write(core, t, index, value); }
            else if (op == "publish") ok = fabric.publish(core, t);
            else if (op == "read") { in >> index; ok = fabric.read(core, t, index, byte); }
            else if (op == "release") ok = fabric.release(core, t);
            else return 2;
        }
        if (!in && op != "snapshot") return 3;
        std::cout << "{\"ok\":" << ok << ",\"token\":[" << output.domain << ',' << output.epoch << ',' << output.generation << ',' << output.index
                  << "],\"message\":[" << message.words[0] << ',' << message.words[1] << ',' << message.words[2] << ',' << message.words[3]
                  << "],\"byte\":" << byte;
        Observer observer; fabric.observe(observer);
    }
    return 0;
}
template<unsigned Features, class Counter> int select(unsigned mode, unsigned domain) {
    if (mode == 0) return run<Features, Owners::Single, Counter>(domain);
    if (mode == 1) return run<Features, Owners::Fixed, Counter>(domain);
    if (mode == 2) return run<Features, Owners::Shared, Counter>(domain);
    return 4;
}
template<class Counter> int features(unsigned f, unsigned mode, unsigned domain) {
    switch (f) {
    case 0: return select<0, Counter>(mode, domain);
    case 1: return select<1, Counter>(mode, domain);
    case 2: return select<2, Counter>(mode, domain);
    case 3: return select<3, Counter>(mode, domain);
    }
    return 5;
}
int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) return 1;
    unsigned mode = std::strtoul(argv[1], nullptr, 10), domain = std::strtoul(argv[2], nullptr, 10);
    unsigned f = argc == 5 ? std::strtoul(argv[4], nullptr, 10) : 3;
    return std::string(argv[3]) == "short" ? features<uint8_t>(f, mode, domain) : features<uint32_t>(f, mode, domain);
}
