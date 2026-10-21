#include "save_coexistence.hpp"
#include "k0_slave_register.hpp"
#include <cassert>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using namespace experiment;
// Generated verbatim from the fixed N03 fixture. No new slave wire contract.
#include "save_k0_registers.inc"
// Fixed X035 medium model, with only physical page size changed to 64 bytes.
#include "save_k0_memory.inc"
template<unsigned Units> struct Geometry {
    static constexpr unsigned max_program_bytes=64;
    static constexpr unsigned program(){return 64;}
    static constexpr unsigned erase(){return 64;}
    static constexpr unsigned units(){return Units;}
    static constexpr uint32_t origin(){return 0x08000000u+16384-Units*64;}
};
template<unsigned C,unsigned Units,unsigned Policy> struct Rig {
    using App=K0Shared<C,1,Gate>;
    App app;Registers io;K0SlaveRegister<App,Registers,Policy,1> port;
    SaveCoexistence<Memory,Geometry<Units>,Units> save;
    uint64_t wall=0,next_input=0;
    uint32_t observed_ms=0,period=0,erase_us=3130,program_us=2810,load_us=86;
    uint32_t generated=0,delivered=0,lost=0,blackouts=0,turns=0;
    unsigned capacity;
    int input_value=1;
    bool coalesced;
    std::deque<int> pending;
    Rig(bool clock,unsigned cap):capacity(cap),coalesced(clock) {
        Gate::masked=Gate::in_irq=false;Gate::depth=0;
        io.held=app.held;app.begin();
        Memory::allowed=[this]{return save.owned();};Memory::pause=[this](unsigned kind){blackout(kind);};
        if constexpr(Units)assert(save.open());
    }
    ~Rig(){save.end();}
    void advance(uint32_t us,bool blocked=false) {
        const auto old=wall;wall+=us;
        const uint32_t ticks=uint32_t(wall/1000-old/1000);
        observed_ms+=blocked && coalesced ? unsigned(ticks!=0) : ticks;
        if(period && wall>=next_input) {
            const auto count=(wall-next_input)/period+1;next_input+=count*period;
            generated+=uint32_t(count);const unsigned room=capacity-unsigned(pending.size());
            const unsigned keep=unsigned(count<room?count:room);
            for(unsigned i=0;i<keep;++i)pending.push_back(input_value);
            lost+=uint32_t(count-keep);
        }
    }
    void blackout(unsigned kind) {
        const auto before=io.log.size();const auto position=uint32_t(app.position),calls=turns;
        const auto mode=port.mode;const auto flags=io.flags;const auto rx=io.rx.size(),tx=io.tx.size();
        if(kind)advance(load_us);
        advance(kind?program_us:erase_us,true);++blackouts;
        assert(before==io.log.size() && position==uint32_t(app.position) && calls==turns);
        assert(mode==port.mode && flags==io.flags && rx==io.rx.size() && tx==io.tx.size());
    }
    void irq() {
        assert(!Gate::depth);const bool prior=Gate::masked;Gate::masked=Gate::in_irq=true;
        port.service(app,io);Gate::in_irq=false;Gate::masked=prior;
    }
    void deliver() {while(!pending.empty()){app.input(int16_t(pending.front()));pending.pop_front();++delivered;}}
    void application(){++turns;app.opportunity(WorkOpportunity::Explicit,WorkProfile::M);}
    void run_until(uint64_t end,uint32_t quantum) {
        assert(quantum && end>=wall);
        while(wall<end){advance(uint32_t(end-wall<quantum?end-wall:quantum));deliver();irq();application();save.progress();}
    }
};
template<unsigned C,unsigned Units,unsigned Policy> void protocol(bool clock,unsigned capacity) {
    Memory::bytes.assign(Units*64,255);Memory::attempted.assign(Units,false);Memory::origin=Geometry<Units>::origin();
    auto rig=std::make_unique<Rig<C,Units,Policy>>(clock,capacity);
    std::string command;int result_out=123;
    while(std::cin>>command) {
        unsigned status=99;Memory::reads=0;Memory::events.clear();rig->io.log.clear();
        auto& r=*rig;
        if(command=="wait"){uint32_t us;std::cin>>us;r.advance(us);}
        else if(command=="rate"){std::cin>>r.period>>r.input_value;assert(r.input_value==1 || r.input_value==-1);r.next_input=r.wall+r.period;}
        else if(command=="latency")std::cin>>r.erase_us>>r.program_us>>r.load_us;
        else if(command=="deliver")r.deliver();
        else if(command=="app")r.application();
        else if(command=="storage")r.save.progress();
        else if(command=="turn"){r.deliver();r.irq();r.application();r.save.progress();}
        else if(command=="run"){uint64_t end;uint32_t q;std::cin>>end>>q;r.run_until(end,q);}
        else if(command=="save"){uint32_t tag;unsigned recover;std::cin>>tag>>recover;status=unsigned(r.save.request(uint32_t(r.app.position),tag,recover!=0));}
        else if(command=="take"){bool ok=false;status=r.save.take(ok);if(status)result_out=ok;}
        else if(command=="cut"){std::cin>>Memory::cut;assert(Memory::cut>=0 && Memory::cut<=258);}
        else if(command=="irq")r.irq();
        else if(command=="write")r.io.address(false);
        else if(command=="read")r.io.address(true);
        else if(command=="byte"){unsigned value;std::cin>>value;r.io.receive(uint8_t(value));}
        else if(command=="consume"){unsigned nack;std::cin>>nack;r.io.consume(nack!=0);}
        else if(command=="stop")r.io.stop();
        else if(command=="input"){int value;std::cin>>value;r.app.input(int16_t(value));}
        else if(command=="rearm")status=r.port.rearm(r.app,r.io);
        else if(command=="recover")status=r.port.recover(r.app,r.io);
        else if(command=="error"){unsigned bits;std::cin>>bits;r.io.flags|=uint16_t(bits);}
        else if(command=="reboot") {
            Memory::allowed=nullptr;Memory::pause=nullptr;rig.reset();Memory::cut=-1;
            rig=std::make_unique<Rig<C,Units,Policy>>(clock,capacity);result_out=123;
        }
        else assert(command=="observe" || command=="dump");
        auto& s=*rig;auto& a=s.app;auto& io=s.io;
        assert(!Gate::depth && !Gate::masked && !Gate::in_irq);
        flash_experiment::Snapshot snapshot{};const bool ready=s.save.peek(snapshot);
        std::cout<<"{\"status\":"<<status<<",\"clock\":["<<s.wall<<','<<s.observed_ms<<','<<s.blackouts<<','<<s.turns
                 <<"],\"input\":["<<s.generated<<','<<s.delivered<<','<<s.lost<<','<<s.pending.size()
                 <<"],\"meaning\":["<<a.position<<','<<a.increment<<','<<a.lower<<','<<a.upper<<','<<a.sequence<<','<<unsigned(a.status)<<','<<a.delta<<','<<a.inbox.size()
                 <<"],\"frontend\":["<<a.writing<<','<<a.reading<<','<<unsigned(a.received)<<','<<unsigned(a.read_index)<<','<<a.accepted
                 <<"],\"port\":["<<unsigned(s.port.mode)<<','<<io.control<<','<<io.flags<<','<<io.busy<<','<<io.wire_ack<<','<<io.peer_byte<<','<<io.rx.size()<<','<<io.tx.size()<<','<<io.releases<<','<<io.release_control
                 <<"],\"published\":[";
        for(unsigned i=0;i<8;++i)std::cout<<(i?",":"")<<unsigned(a.published[i]);
        std::cout<<"],\"save\":[";
        if constexpr(Units)std::cout<<unsigned(s.save.driver.phase())<<','<<unsigned(s.save.driver.scan_phase())<<','<<unsigned(s.save.driver.write_phase())<<','<<s.save.owned()<<','<<s.save.table.occupied()<<','<<s.save.result_held<<','<<s.save.result_ok<<','<<s.save.completed<<','<<s.save.collected;
        else std::cout<<"99,99,99,0,0,0,0,0,0";
        std::cout<<"],\"result_out\":"<<result_out<<",\"snapshot\":["<<ready<<','<<unsigned(snapshot.status)<<','<<snapshot.generation<<','<<unsigned(snapshot.bank)<<','<<flash_experiment::decode(snapshot.bytes,4)<<','<<flash_experiment::decode(snapshot.bytes+4,4)
                 <<"],\"reads\":"<<Memory::reads<<",\"mmio\":"<<io.log.size()<<",\"events\":[";
        for(unsigned i=0;i<Memory::events.size();++i){const auto& e=Memory::events[i];std::cout<<(i?",":"")<<'['<<e.kind<<','<<e.offset<<','<<e.size<<','<<e.success<<']';}
        std::cout<<"],\"memory\":[";
        if(command=="dump")for(unsigned i=0;i<Memory::bytes.size();++i)std::cout<<(i?",":"")<<unsigned(Memory::bytes[i]);
        std::cout<<"]}\n";
    }
    Memory::allowed=nullptr;Memory::pause=nullptr;
}
template<unsigned C,unsigned Units> void policy(unsigned p,bool clock,unsigned cap){if(p)protocol<C,Units,1>(clock,cap);else protocol<C,Units,0>(clock,cap);}
template<unsigned C> void select(unsigned units,unsigned p,bool clock,unsigned cap){
    if(!units)policy<C,0>(p,clock,cap);else if(units==1)policy<C,1>(p,clock,cap);
    else if(units==2)policy<C,2>(p,clock,cap);else{assert(units==4);policy<C,4>(p,clock,cap);}
}
int main(int argc,char** argv){
    assert(argc==6);unsigned c=unsigned(std::strtoul(argv[1],nullptr,10)),u=unsigned(std::strtoul(argv[2],nullptr,10)),p=unsigned(std::strtoul(argv[3],nullptr,10));
    bool clock=std::strtoul(argv[4],nullptr,10)!=0;unsigned cap=unsigned(std::strtoul(argv[5],nullptr,10));assert(p<2 && (cap==1 || cap==4));
    if(c==1)select<1>(u,p,clock,cap);else if(c==2)select<2>(u,p,clock,cap);else if(c==3)select<3>(u,p,clock,cap);else{assert(c==4);select<4>(u,p,clock,cap);}
}
