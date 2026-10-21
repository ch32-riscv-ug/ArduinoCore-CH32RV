#include "k0_slave_register.hpp"
#include <cassert>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>
#include <vector>
using namespace experiment;
struct Gate {
    using Token=bool;
    static inline bool masked=false,in_irq=false;
    static inline unsigned depth=0;
    static void enter(Token& token) { token=masked; masked=true; ++depth; }
    static void leave(Token token) { assert(depth); --depth; masked=token; }
    static void point(unsigned,unsigned) {}
    static void normal() { assert(!depth && !in_irq); }
};
// Explicit model assumptions, not a peripheral emulator. RX two-slot buffering,
// TX prefetch, no STOPF with ACK=0, and NACKed-byte RXNE are selectable/declared.
struct Registers {
    uint16_t flags=0,control=1025;
    bool busy=false,transmit=false,selected=false,armed=false,deliver_nacked=false;
    bool foreign_stop=false,foreign=false;
    int wire_ack=-1,peer_byte=-1;
    unsigned releases=0;
    uint16_t release_control=0;
    const uint8_t* held=nullptr;
    uint8_t release_snapshot[8]{};
    std::deque<uint8_t> rx,tx;
    std::vector<std::string> log;
    void sync() {
        flags=uint16_t(flags & ~(64|128|4));
        if (!rx.empty()) flags|=64;
        if (rx.size()==2) flags|=4;
        if (selected && transmit && !(flags&2) && !(flags&1024)) {
            if (tx.size()<2) flags|=128;
            if (tx.empty()) flags|=4;
        }
    }
    uint16_t read_star1() { log.push_back("SR1"); armed=(flags&2)!=0; return flags; }
    uint16_t read_star2() {
        log.push_back(armed?"SR2-release":"SR2");
        if (armed && (flags&2)) {
            ++releases; release_control=control;
            for (unsigned i=0;i<8;++i) release_snapshot[i]=held[i];
            flags=uint16_t(flags & ~2); sync();
        }
        armed=false; return uint16_t((busy?2:0)|(transmit?4:0));
    }
    uint16_t read_data() {
        assert(!rx.empty()); log.push_back("DR-read");
        uint8_t value=rx.front(); rx.pop_front(); sync(); return value;
    }
    void write_data(uint8_t byte) {
        assert(selected && transmit && tx.size()<2); log.push_back("DR-write");
        tx.push_back(byte); sync();
    }
    uint16_t read_control() { log.push_back("CR-read"); return control; }
    void write_control(uint16_t value) {
        log.push_back((value&1024)?"CR-ACK1":"CR-ACK0"); control=value;
        flags=uint16_t(flags & ~16);
        if (!(value&1)) {
            flags=0; selected=false; transmit=false; armed=false; rx.clear(); tx.clear();
        }
        sync();
    }
    void clear_errors(uint16_t bits) { log.push_back("SR1-W0"); flags=uint16_t(flags & ~bits); sync(); }
    void address(bool reading) {
        assert(!(flags&2)); busy=true; transmit=reading; foreign=false;
        tx.clear(); flags=uint16_t(flags & ~(128|4));
        selected=(control&1025)==1025; wire_ack=selected;
        if (selected) flags|=2;
        sync();
    }
    void receive(uint8_t value) {
        assert(selected && !transmit && !(flags&2)); wire_ack=(control&1024)!=0;
        if (wire_ack || deliver_nacked) {
            if (rx.size()<2) rx.push_back(value); else flags|=2048;
        }
        sync();
    }
    void consume(bool nack) {
        assert(selected && transmit && !tx.empty()); peer_byte=tx.front(); tx.pop_front();
        if (nack) { flags|=1024; tx.clear(); }
        sync();
    }
    void stop() {
        assert(!(flags&2));
        if ((selected && !transmit && (control&1024)) || (foreign && foreign_stop)) flags|=16;
        busy=false; selected=false; transmit=false; tx.clear(); sync();
    }
    void other() {
        assert(!(flags&2)); foreign=true; selected=false; transmit=false; tx.clear(); sync();
    }
};

template<unsigned C,unsigned N,unsigned P> void run(bool nacked,bool foreign_stop) {
    using App=K0Shared<C,N,Gate>;
    App app; Registers io; K0SlaveRegister<App,Registers,P,N> port;
    io.deliver_nacked=nacked; io.foreign_stop=foreign_stop; io.held=app.held;
    app.begin();
    std::string command;
    while (std::cin>>command) {
        io.log.clear(); int returned=-1;
        if (command=="irq") {
            assert(!Gate::depth); Gate::in_irq=true; bool saved=Gate::masked; Gate::masked=true;
            port.service(app,io); Gate::masked=saved; Gate::in_irq=false;
        }
        else if (command=="write") io.address(false);
        else if (command=="read") io.address(true);
        else if (command=="byte") { unsigned value; std::cin>>value; io.receive(uint8_t(value)); }
        else if (command=="consume") { unsigned nack; std::cin>>nack; io.consume(nack!=0); }
        else if (command=="stop") io.stop();
        else if (command=="other") io.other();
        else if (command=="input") { int value; std::cin>>value; app.input(int16_t(value)); }
        else if (command=="step") returned=int(app.opportunity(WorkOpportunity::Explicit,WorkProfile::M));
        else if (command=="opportunity") {
            unsigned where,profile,context;std::cin>>where>>profile>>context;
            returned=int(app.opportunity(WorkOpportunity(where),WorkProfile(profile),WorkContext(context)));
        }
        else if (command=="rearm") returned=port.rearm(app,io);
        else if (command=="recover") returned=port.recover(app,io);
        else if (command=="error") { unsigned bits;std::cin>>bits;io.flags|=uint16_t(bits); }
        else if (command=="sdk_event") { (void)io.read_star1(); (void)io.read_star2(); }
        else { assert(command=="observe"); }
        assert(!Gate::depth && !Gate::masked && !Gate::in_irq);
        std::cout<<"{\"meaning\":["<<app.position<<','<<app.increment<<','<<app.lower<<','<<app.upper<<','
                 <<app.sequence<<','<<unsigned(app.status)<<','<<app.delta<<','<<app.inbox.size()<<"],\"frontend\":["
                 <<app.writing<<','<<app.reading<<','<<unsigned(app.received)<<','<<unsigned(app.read_index)<<','<<app.accepted
                 <<"],\"port\":["<<unsigned(port.mode)<<','<<io.control<<','<<io.flags<<','<<io.busy<<','<<io.wire_ack<<','
                 <<io.peer_byte<<','<<io.rx.size()<<','<<io.tx.size()<<','<<returned<<','<<io.releases<<','<<io.release_control
                 <<"],\"published\":[";
        for(unsigned i=0;i<8;++i)std::cout<<(i?",":"")<<unsigned(app.published[i]);
        std::cout<<"],\"release_snapshot\":[";
        for(unsigned i=0;i<8;++i)std::cout<<(i?",":"")<<unsigned(io.release_snapshot[i]);
        std::cout<<"],\"log\":[";
        for(unsigned i=0;i<io.log.size();++i)std::cout<<(i?",":"")<<'"'<<io.log[i]<<'"';
        std::cout<<"]}\n";
    }
}
template<unsigned C,unsigned N> void policy(unsigned p,bool n,bool f) { if(p)run<C,N,1>(n,f);else run<C,N,0>(n,f); }
template<unsigned C> void capacity(unsigned n,unsigned p,bool b,bool f) { if(n==1)policy<C,1>(p,b,f);else{assert(n==2);policy<C,2>(p,b,f);} }
int main(int argc,char** argv) {
    assert(argc==6);unsigned c=unsigned(std::strtoul(argv[1],nullptr,10)),n=unsigned(std::strtoul(argv[2],nullptr,10));
    unsigned p=unsigned(std::strtoul(argv[3],nullptr,10));assert(p<2);
    bool b=std::strtoul(argv[4],nullptr,10)!=0,f=std::strtoul(argv[5],nullptr,10)!=0;
    if(c==1)capacity<1>(n,p,b,f);else if(c==2)capacity<2>(n,p,b,f);else if(c==3)capacity<3>(n,p,b,f);else{assert(c==4);capacity<4>(n,p,b,f);}
}
