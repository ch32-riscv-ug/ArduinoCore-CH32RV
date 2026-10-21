#include "i2c_managed_handoff.hpp"
#include "completion_delivery.hpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
using namespace i2c_experiment;
#include "i2c_handoff_fake.inc"

template<class Domain> struct CheckedBus:Fake {
    static inline Domain* domain=nullptr;
    static inline const void* owner=nullptr;
    static inline bool (*held)(const void*)=nullptr;
    static void check(){assert(domain&&domain->count()>=1&&domain->held()==7&&held&&held(owner));}
    static uint16_t read16(uintptr_t p){check();return Fake::read16(p);}
    static void write16(uintptr_t p,uint16_t v){check();Fake::write16(p,v);}
    static unsigned critical_enter(){check();return Fake::critical_enter();}
    static void critical_leave(unsigned saved){check();Fake::critical_leave(saved);}
};
template<class Domain> struct Pins {
    Domain* domain;bool valid=false;unsigned checks=0;
    bool ready(){assert(domain->count()>=1&&domain->held()==7);++checks;return valid;}
};
template<class Driver> struct Delivery {
    using Events=experiment::EventExecutor<typename Driver::Token,2,1>;
    struct Context {Delivery* self=nullptr;Driver* driver=nullptr;unsigned source=0;};
    struct Call {unsigned source,generation,index,client,success;};
    Events events;
    typename Events::Token targets[2]{};
    Context contexts[2];
    unsigned calls[2]{},collected[2]{},skipped[2]{};
    typename Driver::Completion last[2]{};
    std::vector<Call> log;
    bool recurse=false;
    experiment::TurnStatus recursive=experiment::TurnStatus::Ran;
    explicit Delivery(Driver* drivers){
        for(unsigned i=0;i<2;++i){contexts[i]={this,&drivers[i],i};targets[i]=Events::invalid();}
    }
    static void handle(void* user,const typename Events::Delivery& delivery){
        auto& context=*static_cast<Context*>(user);auto& self=*context.self;const unsigned i=context.source;
        assert(delivery.has_message);++self.calls[i];
        const bool ok=completion_delivery_experiment::collect(*context.driver,delivery.message,self.last[i]);
        if(ok)++self.collected[i];else ++self.skipped[i];
        const auto t=delivery.message;self.log.push_back({i,unsigned(t.generation),t.index,t.client,unsigned(ok)});
        if(self.recurse)self.recursive=self.events.turn(2,experiment::WorkOpportunity::Explicit,experiment::WorkProfile::U).status;
    }
};
template<bool Dedicated,class Counter> int run(){
    constexpr unsigned Records=Dedicated?3:2;
    using Domain=i2c_resource_experiment::Domain<2,Counter>;
    using Bus=CheckedBus<Domain>;
    using Core=i2c_handoff_experiment::Manager<Bus,3,Records,Dedicated,64>;
    using Driver=i2c_managed_handoff_experiment::Managed<Core,Domain>;
    Domain resources;Pins<Domain> pins{&resources};Driver drivers[2];unsigned selected=0;
    Delivery<Driver> delivery(drivers);
    int outsider=0;typename Domain::Ticket external{};
    Bus::domain=&resources;Bus::held=[](const void* p){return static_cast<const Driver*>(p)->held();};
    typename Driver::Storage buffers[3];typename Driver::Token token{99,99,99};
    Completion output{Result::Failure,77,78,79,80};uint8_t value=123;
    for(std::string line;std::getline(std::cin,line);){
        assert(!Fake::depth);Fake::events.clear();std::istringstream in(line);std::string op;in>>op;
        unsigned a=0,b=0,c=0,d=0,e=0,status=99;
        if(op=="select"){in>>selected;assert(selected<2);}
        auto& driver=drivers[selected];Bus::owner=&driver;
        if(op=="backend"){in>>a;status=unsigned(driver.open_backend(resources,pins,a));}
        else if(op=="select")status=1;
        else if(op=="initialize"){delivery.events.initialize();status=1;}
        else if(op=="bind"){
            in>>a>>b;assert(a<2);delivery.targets[selected]=delivery.events.add(Delivery<Driver>::handle,&delivery.contexts[selected],experiment::WorkDomain(a),b!=0);
            status=delivery.events.valid(delivery.targets[selected]);
        }else if(op=="unbind")status=delivery.events.remove(delivery.targets[selected]);
        else if(op=="pump"||op=="pump_to"||op=="direct_collect"){
            unsigned destination=selected;if(op=="pump_to"){in>>destination;assert(destination<2);}
            in>>a>>b>>c;assert(b<=255&&c<=255);typename Driver::Token request{a,uint8_t(b),uint8_t(c)};
            if(op=="direct_collect")status=completion_delivery_experiment::collect(driver,request,output);
            else status=unsigned(completion_delivery_experiment::pump(driver,delivery.events,delivery.targets[destination],request));
        }else if(op=="turn"){
            in>>a>>b>>c>>d;assert(a<=2&&b<3&&c<3&&d<4);
            const auto result=delivery.events.turn(a,experiment::WorkOpportunity(b),experiment::WorkProfile(c),experiment::WorkContext(d));
            status=unsigned(result.status);
        }else if(op=="recurse"){in>>a;delivery.recurse=a!=0;status=1;}
        else if(op=="change")status=resources.begin_change();
        else if(op=="publish"){in>>a;status=resources.publish(a);}
        else if(op=="release")status=resources.release(&outsider,external);
        else if(op=="pins"){in>>a;pins.valid=a!=0;status=1;}
        else if(op=="claim"){in>>a;status=resources.claim(&outsider,a,external);}
        else if(op=="close")status=driver.close_backend();
        else if(op=="open"){in>>a;status=driver.open(a);}
        else if(op=="end"){in>>a;status=driver.end(a);}
        else if(op=="submit"){
            in>>a>>b>>c>>d>>e;assert(b<=3);status=unsigned(driver.submit(a,b==3?nullptr:&buffers[b],c,d,e,token));
        }else if(op=="dispatch")status=unsigned(driver.dispatch(token));
        else if(op=="step")driver.step();
        else if(op=="reap")status=driver.reap();
        else if(op=="cancel"||op=="notice"||op=="ack"||op=="take"){
            in>>a>>b>>c;assert(b<=255&&c<=255);typename Driver::Token request{uint32_t(a),uint8_t(b),uint8_t(c)};
            if(op=="cancel")status=unsigned(driver.cancel(request));
            else if(op=="notice")status=driver.notice(request);
            else if(op=="ack")status=driver.ack(request);
            else status=driver.take(request,output);
        }else if(op=="write"){in>>a>>b>>c;assert(a<3);status=buffers[a].write(b,uint8_t(c));}
        else if(op=="read"){in>>a>>b;assert(a<3);status=buffers[a].read(b,value);}
        else if(op=="event"){
            in>>a>>b;Fake::regs[S1/4]=uint16_t(a);Fake::regs[S2/4]=uint16_t(b);Fake::seen=false;
            if(a&SB)Fake::regs[C1/4]&=uint16_t(~START);
            if(a&ARLO)Fake::regs[C1/4]&=uint16_t(~(START|STOP));
            if(!Fake::fifo.empty())Fake::refresh();
        }else if(op=="push"){
            in>>a;for(unsigned i=0;i<a;++i){in>>b;Fake::fifo.push_back(uint8_t(b));}assert(Fake::fifo.size()<=2);Fake::refresh();
        }else if(op=="quiet"){
            in>>a;Fake::regs[C1/4]&=uint16_t(~(START|STOP));Fake::regs[S2/4]=a?2:0;Fake::regs[S1/4]=TXE;
            Fake::refresh();Fake::seen=false;
        }else if(op=="control"){in>>a;Fake::regs[C1/4]=uint16_t(a);}
        else assert(op=="snapshot");
        assert(in);
        std::cout<<"{\"status\":"<<status<<",\"token\":["<<unsigned(token.generation)<<','<<unsigned(token.index)<<','<<unsigned(token.client)
                 <<"],\"backend\":["<<driver.backend_opened()<<','<<driver.backend_active()<<','<<driver.backend_terminal()<<','<<driver.backend_phase()<<','<<driver.backend_generation()
                 <<"],\"current\":"<<driver.current()<<",\"cursor\":"<<driver.cursor()<<",\"opened\":[";
        for(unsigned i=0;i<3;++i)std::cout<<(i?",":"")<<driver.opened(i);
        std::cout<<"],\"records\":[";
        for(unsigned i=0;i<Records;++i)std::cout<<(i?",":"")<<'['<<driver.phase(i)<<','<<driver.owner(i)<<','<<driver.generation(i)<<']';
        std::cout<<"],\"out\":["<<unsigned(output.result)<<','<<output.issued<<','<<output.confirmed<<','<<output.received<<','<<output.flags<<"],\"value\":"<<unsigned(value)<<",\"bytes\":[";
        for(unsigned bfr=0;bfr<3;++bfr){
            std::cout<<(bfr?",":"")<<'[';
            for(unsigned i=0;i<64;++i){uint8_t v=0;const bool available=buffers[bfr].read(i,v);std::cout<<(i?",":"")<<(available?int(v):-1);}
            std::cout<<']';
        }
        std::cout<<"],\"regs\":[";for(unsigned i=0;i<8;++i)std::cout<<(i?",":"")<<Fake::regs[i];
        std::cout<<"],\"tx\":[";for(unsigned i=0;i<Fake::transmitted.size();++i)std::cout<<(i?",":"")<<Fake::transmitted[i];
        std::cout<<"],\"addr_ctl\":[";for(unsigned i=0;i<Fake::addr_control.size();++i)std::cout<<(i?",":"")<<Fake::addr_control[i];
        std::cout<<"],\"events\":[";
        for(unsigned i=0;i<Fake::events.size();++i){auto event=Fake::events[i];std::cout<<(i?",":"")<<'['<<event.kind<<','<<event.offset<<','<<event.value<<']';}
        std::cout<<"],\"resource\":["<<resources.frequency()<<','<<resources.epoch()<<','<<resources.count()<<','<<resources.held()<<','<<resources.changing()
                 <<"],\"selected\":"<<selected<<",\"held\":["<<drivers[0].held()<<','<<drivers[1].held()<<"],\"pin_checks\":"<<pins.checks;
        std::cout<<",\"targets\":[";
        for(unsigned i=0;i<2;++i){auto t=delivery.targets[i];std::cout<<(i?",":"")<<'['<<t.index<<','<<t.generation<<','<<delivery.events.valid(t)<<','<<delivery.events.queued(t)<<']';}
        std::cout<<"],\"callbacks\":[";
        for(unsigned i=0;i<2;++i)std::cout<<(i?",":"")<<'['<<delivery.calls[i]<<','<<delivery.collected[i]<<','<<delivery.skipped[i]<<']';
        std::cout<<"],\"callback_out\":[";
        for(unsigned i=0;i<2;++i){auto v=delivery.last[i];std::cout<<(i?",":"")<<'['<<unsigned(v.result)<<','<<v.issued<<','<<v.confirmed<<','<<v.received<<','<<v.flags<<']';}
        std::cout<<"],\"callback_log\":[";
        for(unsigned i=0;i<delivery.log.size();++i){auto v=delivery.log[i];std::cout<<(i?",":"")<<'['<<v.source<<','<<v.generation<<','<<v.index<<','<<v.client<<','<<v.success<<']';}
        std::cout<<"],\"recursive\":"<<unsigned(delivery.recursive)<<"}\n";
    }
    return 0;
}
template<bool D> int generations(const std::string& mode){
    if(mode=="domain8")return run<D,uint8_t>();
    assert(mode=="normal");return run<D,uint32_t>();
}
int main(int argc,char** argv){
    assert(argc==3);if(std::string(argv[1])=="dedicated")return generations<true>(argv[2]);
    assert(std::string(argv[1])=="pooled");return generations<false>(argv[2]);
}
