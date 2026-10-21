#include "k0_shared.hpp"
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
using namespace experiment;
struct HostGate {
    using Token = bool;
    static inline bool protect=true,masked=false,pending=false,armed=false,in_irq=false;
    static inline unsigned depth=0,tag=0,index=0,attempts=0,delivered=0,deferred=0,normal_calls=0;
    static inline int kind=0,value=0;
    static inline std::function<void(int,int)> callback;
    static void dispatch() {
        assert(callback && !in_irq);
        const bool saved = masked;
        masked = true; in_irq = true; ++delivered;
        callback(kind,value);
        in_irq = false; masked = saved;
    }
    static void enter(Token& saved) { saved=masked; ++depth; if(protect) masked=true; }
    static void leave(Token saved) {
        assert(depth); --depth; masked=saved;
        if(!masked && !depth && pending) { pending=false; dispatch(); }
    }
    static void point(unsigned t,unsigned i) {
        if(!armed || t!=tag || i!=index) return;
        armed=false; ++attempts;
        if(masked) { assert(!pending); pending=true; ++deferred; }
        else dispatch();
    }
    static void normal() { assert(!depth && !in_irq); ++normal_calls; }
};
template<class App> void protocol(WorkDomain domain, bool opt) {
    App app;
    HostGate::callback = [&app](int kind,int value) {
        if (kind == 0) app.input(int16_t(value));
        else if (kind == 1) app.begin_read();
        else if (kind == 2) app.begin_write();
        else app.stop();
    };
    TurnStatus status = TurnStatus::Ran;
    std::string command;
    while (std::cin >> command) {
        if (command == "arm") {
            std::cin >> HostGate::tag >> HostGate::index >> HostGate::kind >> HostGate::value;
            HostGate::armed = true;
        }
        else if (command == "mask") { unsigned value; std::cin >> value; assert(!HostGate::depth); HostGate::masked=value!=0; }
        else if (command == "nested") {
            const bool prior=HostGate::masked; HostGate::Token a,b;
            HostGate::enter(a); HostGate::enter(b); HostGate::leave(b); HostGate::leave(a);
            assert(!HostGate::depth && HostGate::masked==prior);
        }
        else if (command == "begin") app.begin(domain, opt);
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

        std::cout << "{\"state\":[" << app.position << ',' << app.increment << ',' << app.lower << ',' << app.upper << ','
                  << app.sequence << ',' << unsigned(app.status) << ',' << app.delta << ',' << app.inbox.size() << ','
                  << app.reading << ',' << unsigned(app.read_index) << ',' << app.reply << ',' << app.accepted << ',' << unsigned(status)
                  << "],\"published\":[";
        for (unsigned i=0;i<8;++i) std::cout << (i?",":"") << unsigned(app.published[i]);
        std::cout << "],\"held\":[";
        for (unsigned i=0;i<8;++i) std::cout << (i?",":"") << unsigned(app.held[i]);
        std::cout << "],\"gate\":[" << HostGate::masked << ',' << HostGate::depth << ',' << HostGate::pending << ',' << HostGate::armed
                  << ',' << HostGate::attempts << ',' << HostGate::delivered << ',' << HostGate::deferred << ',' << HostGate::normal_calls << "]}\n";
    }
    HostGate::callback = nullptr;
}

template<unsigned N> void select(const std::string& candidate,WorkDomain domain,bool opt) {
    if(candidate=="R") protocol<K0Shared<1,N,HostGate>>(domain,opt);
    else if(candidate=="A") protocol<K0Shared<2,N,HostGate>>(domain,opt);
    else if(candidate=="B") protocol<K0Shared<3,N,HostGate>>(domain,opt);
    else { assert(candidate=="C-bridge"); protocol<K0Shared<4,N,HostGate>>(domain,opt); }
}
int main(int argc,char** argv) {
    assert(argc==6);
    HostGate::protect=std::string(argv[1])=="guarded";
    if(!HostGate::protect) assert(std::string(argv[1])=="unguarded");
    const unsigned capacity=unsigned(std::strtoul(argv[3],nullptr,10));
    const auto domain=WorkDomain(std::strtoul(argv[4],nullptr,10)); const bool opt=std::strtoul(argv[5],nullptr,10)!=0;
    if(capacity==1) select<1>(argv[2],domain,opt);
    else { assert(capacity==2); select<2>(argv[2],domain,opt); }
}
