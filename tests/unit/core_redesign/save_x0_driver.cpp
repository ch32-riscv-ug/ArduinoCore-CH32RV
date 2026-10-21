#include "save_coexistence.hpp"
#include "x0_prepared_wire.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using namespace experiment;
using namespace i2c_experiment;
#include "i2c_handoff_fake.inc"
#include "x035_route_fake.inc"
#include "x0_prepared_helpers.inc"

struct Memory {
    static inline std::vector<uint8_t> bytes;
    static inline std::vector<bool> attempted;
    static inline uint32_t origin=0,reads=0;
    static inline int cut=-1,unstable=-1;
    static inline unsigned unstable_reads=0;
    static inline bool in_operation=false;
    static inline std::function<bool()> allowed;
    static inline std::function<void(unsigned)> pause;
    struct Event { unsigned kind,offset,size;bool success; };
    static inline std::vector<Event> events;
    static uint8_t read(uint32_t address) {
        assert(allowed && allowed() && !in_operation && address>=origin && address-origin<bytes.size());
        ++reads;const unsigned offset=address-origin;
        return offset==unsigned(unstable) ? uint8_t(bytes[offset]^(++unstable_reads&1)) : bytes[offset];
    }
    static void stall(unsigned kind) { assert(!in_operation);in_operation=true;pause(kind);in_operation=false; }
    static uint8_t mask(unsigned index,unsigned size) { return cut==256 ? (index<size/2?255:0) : uint8_t(cut); }
    static bool erase(uint32_t address,unsigned size) {
        assert(allowed && allowed() && size==256 && address>=origin);
        const unsigned offset=address-origin;assert(offset%256==0 && offset+size<=bytes.size());
        stall(0);const bool ok=cut<0;
        for(unsigned i=0;i<size;++i) {
            if(ok)bytes[offset+i]=255;
            else if(cut>=257)bytes[offset+i]=0;
            else bytes[offset+i]|=mask(i,size);
        }
        attempted[offset/256]=!ok;
        if(unstable>=int(offset) && unstable<int(offset+size))unstable=-1;
        if(cut==258){unstable=int(offset);unstable_reads=0;}
        events.push_back({0,offset,size,ok});cut=-1;return ok;
    }
    static bool program(uint32_t address,const uint8_t* data,unsigned size) {
        assert(allowed && allowed() && size==256 && address>=origin && cut<=256);
        const unsigned offset=address-origin;assert(offset%256==0 && offset+size<=bytes.size() && !attempted[offset/256]);
        for(unsigned i=0;i<size;++i)assert(bytes[offset+i]==255);
        attempted[offset/256]=true;stall(1);const bool ok=cut<0;
        for(unsigned i=0;i<size;++i)bytes[offset+i]&=uint8_t(data[i]|(ok?0:uint8_t(255^mask(i,size))));
        events.push_back({1,offset,size,ok});cut=-1;return ok;
    }
};
template<unsigned Units> struct Geometry {
    static constexpr unsigned max_program_bytes=256;
    static constexpr unsigned program(){return 256;}
    static constexpr unsigned erase(){return 256;}
    static constexpr unsigned units(){return Units;}
    static constexpr uint32_t origin(){return 0x08000000u+63488-Units*256;}
};
template<unsigned Candidate,unsigned Units> struct Rig {
    PreparedX0<Candidate,Checked,0,x035_i2c_route_experiment::Package::C8,false> app;
    SaveCoexistence<Memory,Geometry<Units>,Units> save;
    uint64_t wall=0,next_input=0;
    uint32_t observed_ms=0,erase_us=2920,program_us=1620,load_us=68,period=0;
    uint32_t lost=0,generated=0,delivered=0,turns=0,blackouts=0;
    unsigned input_capacity;
    int input_kind=1;
    bool coalesced;
    std::deque<int> pending;
    Rig(bool clock,unsigned capacity):input_capacity(capacity),coalesced(clock) {
        for(auto& value:Fake::regs)value=0;
        Fake::depth=0;Fake::seen=false;Fake::fifo.clear();Fake::events.clear();Fake::transmitted.clear();Fake::addr_control.clear();
        for(auto& value:BoardBus::words)value=0;
        BoardBus::words[0]=3;BoardBus::ignored=0;BoardBus::accesses.clear();
        assert(app.bus.initialize());app.begin();
        Checked::read_probe=false;Checked::resources=&app.bus.resources;
        Checked::holder=&app.bus.preparation;Checked::held=[](void* p){return static_cast<typename decltype(app.bus)::Preparation*>(p)->held();};
        assert(unsigned(app.bus.preparation.prepare(true))==0);
        Checked::holder=&app.bus.driver;Checked::held=[](void* p){return static_cast<typename decltype(app.bus)::Driver*>(p)->held();};
        Memory::allowed=[this]{return save.owned();};Memory::pause=[this](unsigned kind){blackout(kind);};
        if constexpr(Units)assert(save.open());
    }
    ~Rig(){save.end();}
    void advance(uint32_t us,bool blocked=false) {
        const auto old=wall;wall+=us;
        const uint32_t ticks=uint32_t(wall/1000-old/1000);
        observed_ms+=blocked && coalesced ? unsigned(ticks!=0) : ticks;
        if(period && wall>=next_input) {
            const uint64_t count=(wall-next_input)/period+1;next_input+=count*period;
            generated+=uint32_t(count);const unsigned room=input_capacity-unsigned(pending.size());
            const unsigned keep=unsigned(count<room?count:room);
            for(unsigned i=0;i<keep;++i)pending.push_back(input_kind);
            lost+=uint32_t(count-keep);
        }
        // Frozen virtual devices use wall time. Only periodic application
        // scheduling uses the separately observed millisecond counter.
        app.hardware(uint32_t(wall/1000));app.now=observed_ms;
    }
    void blackout(unsigned kind) {
        const auto pps=app.pps.reclaimed,wire=app.bus.reclaimed,measure=app.measurement_done,frames=app.screen.frame_count;
        const auto calls=turns;
        const auto mmio=Fake::events.size(),board=BoardBus::accesses.size();
        if(kind)advance(load_us);
        advance(kind?program_us:erase_us,true);++blackouts;
        assert(pps==app.pps.reclaimed && wire==app.bus.reclaimed && measure==app.measurement_done && frames==app.screen.frame_count);
        assert(calls==turns && mmio==Fake::events.size() && board==BoardBus::accesses.size());
    }
    void deliver() { while(!pending.empty()){app.pps.enqueue(int16_t(pending.front()));pending.pop_front();++delivered;}app.changed(); }
    void application() { ++turns;app.changed();app.opportunity(WorkOpportunity::Explicit,WorkProfile::M); }
    void run_until(uint64_t end,uint32_t quantum) {
        assert(quantum && end>=wall);
        while(wall<end) {
            advance(uint32_t(end-wall<quantum?end-wall:quantum));deliver();
            if(app.bus.active && !app.bus.terminal && app.bus.packet.kind==X0WireKind::I2c) {
                const uint16_t word=app.bus.packet.address==0x40 && app.bus.packet.tx[0]==4 ? 1000 : 10000;
                transfer(app.bus,app.bus.packet.tx_size,app.bus.packet.rx_size,word,0,0);
            }
            application();save.progress();
        }
    }
};
template<unsigned Candidate,unsigned Units> void protocol(bool coalesced,unsigned capacity) {
    Memory::bytes.assign(Units*256,255);Memory::attempted.assign(Units,false);Memory::origin=Geometry<Units>::origin();
    auto rig=std::make_unique<Rig<Candidate,Units>>(coalesced,capacity);
    std::string command;int result_out=123;
    while(std::cin>>command) {
        unsigned status=99;Memory::events.clear();Memory::reads=0;Fake::events.clear();BoardBus::accesses.clear();
        auto& app=rig->app;
        if(command=="attach")app.pps.attach();
        else if(command=="detach")app.pps.detach();
        else if(command=="key"){int value;std::cin>>value;status=app.pps.enqueue(int16_t(value));}
        else if(command=="wait"){uint32_t us;std::cin>>us;rig->advance(us);}
        else if(command=="rate"){std::cin>>rig->period>>rig->input_kind;assert(rig->input_kind==1 || rig->input_kind==-1);rig->next_input=rig->wall+rig->period;}
        else if(command=="latency")std::cin>>rig->erase_us>>rig->program_us>>rig->load_us;
        else if(command=="deliver")rig->deliver();
        else if(command=="app")rig->application();
        else if(command=="storage")rig->save.progress();
        else if(command=="turn"){rig->deliver();rig->application();rig->save.progress();}
        else if(command=="run"){uint64_t end;uint32_t quantum;std::cin>>end>>quantum;rig->run_until(end,quantum);}
        else if(command=="save"){uint32_t tag;unsigned recover;std::cin>>tag>>recover;status=unsigned(rig->save.request(uint32_t(app.pps.desired),tag,recover!=0));}
        else if(command=="take"){bool ok=false;status=rig->save.take(ok);if(status)result_out=ok;}
        else if(command=="cut"){std::cin>>Memory::cut;assert(Memory::cut>=0 && Memory::cut<=258);}
        else if(command=="finish") {
            if(app.bus.active && !app.bus.terminal && app.bus.packet.kind==X0WireKind::I2c) {
                uint16_t word=app.bus.packet.address==0x40 && app.bus.packet.tx[0]==4 ? 1000 : 10000;
                transfer(app.bus,app.bus.packet.tx_size,app.bus.packet.rx_size,word,0,0);
            }
        }
        else if(command=="reboot") {
            Memory::allowed=nullptr;Memory::pause=nullptr;rig.reset();Memory::cut=-1;
            rig=std::make_unique<Rig<Candidate,Units>>(coalesced,capacity);result_out=123;
        }
        else assert(command=="observe" || command=="dump");
        auto& r=*rig;auto& a=r.app;a.changed();
        flash_experiment::Snapshot snapshot{};const bool ready=r.save.peek(snapshot);
        std::cout<<"{\"status\":"<<status<<",\"clock\":["<<r.wall<<','<<r.observed_ms<<','<<r.blackouts<<','<<r.turns
                 <<"],\"input\":["<<r.generated<<','<<r.delivered<<','<<r.lost<<','<<r.pending.size()
                 <<"],\"pps\":["<<a.pps.desired<<','<<a.pps.confirmed<<','<<a.pps.valid<<','<<a.pps.stale<<','<<a.pps.inbox.size()<<','
                 <<a.pps.accepted_count<<','<<a.pps.rejected_count<<','<<a.pps.started<<','<<a.pps.reclaimed<<','<<a.pps.active<<','<<a.pps.terminal
                 <<"],\"wire\":["<<a.bus.active<<','<<a.bus.terminal<<','<<unsigned(a.bus.owner)<<','<<a.bus.started<<','<<a.bus.completed<<','<<a.bus.reclaimed<<','
                 <<a.bus.driver.held()<<','<<a.bus.resources.count()<<','<<!a.bus.storage.available()
                 <<"],\"periodic\":["<<a.measurement_done<<','<<a.touch_done<<','<<a.missed_measure<<','<<a.missed_touch<<','<<a.screen.frame_count
                 <<"],\"save\":[";
        if constexpr(Units)std::cout<<unsigned(r.save.driver.phase())<<','<<unsigned(r.save.driver.scan_phase())<<','<<unsigned(r.save.driver.write_phase())<<','<<r.save.owned()<<','
                                 <<r.save.table.occupied()<<','<<r.save.result_held<<','<<r.save.result_ok<<','<<r.save.completed<<','<<r.save.collected;
        else std::cout<<"99,99,99,0,0,0,0,0,0";
        std::cout<<"],\"result_out\":"<<result_out<<",\"snapshot\":["<<ready<<','<<unsigned(snapshot.status)<<','<<snapshot.generation<<','<<unsigned(snapshot.bank)<<','
                 <<flash_experiment::decode(snapshot.bytes,4)<<','<<flash_experiment::decode(snapshot.bytes+4,4)
                 <<"],\"reads\":"<<Memory::reads<<",\"mmio\":"<<Fake::events.size()+BoardBus::accesses.size()<<",\"events\":[";
        for(unsigned i=0;i<Memory::events.size();++i){const auto& e=Memory::events[i];std::cout<<(i?",":"")<<'['<<e.kind<<','<<e.offset<<','<<e.size<<','<<e.success<<']';}
        std::cout<<"],\"memory\":[";
        if(command=="dump")for(unsigned i=0;i<Memory::bytes.size();++i)std::cout<<(i?",":"")<<unsigned(Memory::bytes[i]);
        std::cout<<"]}\n";
    }
    Memory::allowed=nullptr;Memory::pause=nullptr;
}
template<unsigned Candidate> void select(unsigned units,bool clock,unsigned capacity) {
    if(units==0)protocol<Candidate,0>(clock,capacity);else if(units==1)protocol<Candidate,1>(clock,capacity);
    else if(units==2)protocol<Candidate,2>(clock,capacity);else{assert(units==4);protocol<Candidate,4>(clock,capacity);}
}
int main(int argc,char** argv) {
    assert(argc==5);const unsigned candidate=unsigned(std::strtoul(argv[1],nullptr,10)),units=unsigned(std::strtoul(argv[2],nullptr,10)),capacity=unsigned(std::strtoul(argv[4],nullptr,10));
    const bool clock=std::strtoul(argv[3],nullptr,10)!=0;assert(capacity==1 || capacity==4);
    if(candidate==1)select<1>(units,clock,capacity);else if(candidate==2)select<2>(units,clock,capacity);
    else if(candidate==3)select<3>(units,clock,capacity);else{assert(candidate==4);select<4>(units,clock,capacity);}
}
