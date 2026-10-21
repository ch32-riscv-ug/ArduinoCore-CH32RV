#include "h417_executor.hpp"
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
using namespace h417_executor_experiment;
struct Hook {
    static inline std::function<void(unsigned,unsigned)> callback;
    static void after_probe(unsigned core,unsigned mask){if(callback)callback(core,mask);}
};
#include "h417_executor_observer.inc"
template<unsigned C,Owners P,unsigned F,class Counter,bool Safe> int run(unsigned domain,bool opt) {
    Application<C,P,F,Counter,Safe,Hook> app;app.begin(WorkDomain(domain),opt);
    Token slots[2]{};Ticket ticket{};
    bool armed=false;unsigned injected_core=0,injected_value=0;
    Hook::callback=[&](unsigned core,unsigned mask){
        if(!armed || mask!=1)return;
        armed=false;Ticket t{};
        assert(app.push(injected_core,core,injected_value,t));assert(app.hint(t));
    };
    for(std::string line;std::getline(std::cin,line);) {
        std::istringstream input(line);std::string command;uint32_t a[6]{};input>>command;for(auto& value:a)input>>value;
        int status=99;
        if(command=="push")status=app.push(a[0],a[1],a[2],ticket);
        else if(command=="reserve"){assert(a[1]<2);status=app.reserve(a[0],slots[a[1]]);}
        else if(command=="write"){assert(a[1]<2);status=app.write(a[0],slots[a[1]],a[2],a[3]);}
        else if(command=="fill") {
            assert(a[1]<2);status=1;
            for(unsigned i=0;i<64;++i)if(!app.write(a[0],slots[a[1]],i,uint8_t(a[2]+i))){status=0;break;}
        }
        else if(command=="publish"){assert(a[1]<2);status=app.publish(a[0],slots[a[1]],a[2],ticket);}
        else if(command=="notify")status=app.hint(ticket);
        else if(command=="hint")status=app.hint({a[0],a[1],a[2]});
        else if(command=="turn"){assert(a[0]<2);status=int(app.workers[a[0]].opportunity(WorkOpportunity(a[1]),WorkProfile(a[2]),WorkContext(a[3])));}
        else if(command=="gate"){app.gate.blocked=a[0]!=0;}
        else if(command=="online"){assert(a[0]<2);app.workers[a[0]].online=a[1]!=0;}
        else if(command=="recover")status=app.recover();
        else if(command=="dma"){app.dma_quiet=a[0]!=0;}
        else if(command=="attest")status=app.attest(a[0]);
        else if(command=="restart")status=app.restart();
        else if(command=="inject"){armed=true;injected_core=a[0];injected_value=a[1];}
        else assert(command=="inspect");
        assert(!app.gate.held);
        std::cout<<"{\"status\":"<<status<<",\"session\":["<<unsigned(app.epoch)<<','<<app.recovering<<','<<unsigned(app.hints[0])<<','<<unsigned(app.hints[1])<<','<<app.dma_quiet
                 <<"],\"ticket\":["<<ticket.epoch<<','<<ticket.mask<<','<<ticket.destination<<"],\"slots\":[";
        for(unsigned i=0;i<2;++i)std::cout<<(i?",":"")<<'['<<slots[i].domain<<','<<slots[i].epoch<<','<<slots[i].generation<<','<<slots[i].index<<']';
        std::cout<<"],\"armed\":"<<armed<<",\"workers\":[";
        for(unsigned i=0;i<2;++i) {
            const auto& w=app.workers[i];std::cout<<(i?",":"")<<'['<<w.online<<','<<w.calls<<','<<w.busy;
            if constexpr(F&1)std::cout<<','<<w.messages<<','<<w.message_hash;else std::cout<<",0,0";
            if constexpr(F&2)std::cout<<','<<w.holding<<','<<unsigned(w.cursor)<<','<<w.bytes<<','<<w.blocks<<','<<w.block_hash<<','<<w.invalidated<<','<<w.lease.epoch<<','<<w.lease.generation<<','<<w.lease.index;
            else std::cout<<",0,0,0,0,0,0,0,0,0";
            std::cout<<']';
        }
        std::cout<<']';Observer observer;app.fabric.observe(observer);
    }
    Hook::callback=nullptr;return 0;
}
template<unsigned C,Owners P,unsigned F,class Counter> int safety(bool safe,unsigned d,bool o){
    if(safe)return run<C,P,F,Counter,true>(d,o);
    if constexpr(F==1 && std::is_same<Counter,uint32_t>::value)return run<C,P,F,Counter,false>(d,o);
    assert(false);return 2;
}
template<unsigned C,Owners P,unsigned F> int counter(bool short_counter,bool safe,unsigned d,bool o){return short_counter?safety<C,P,F,uint8_t>(safe,d,o):safety<C,P,F,uint32_t>(safe,d,o);}
template<unsigned C,Owners P> int features(unsigned f,bool s,bool safe,unsigned d,bool o) {
    if(f==0)return counter<C,P,0>(s,safe,d,o);
    if(f==1)return counter<C,P,1>(s,safe,d,o);
    if(f==2)return counter<C,P,2>(s,safe,d,o);
    assert(f==3);return counter<C,P,3>(s,safe,d,o);
}
template<unsigned C> int policy(unsigned p,unsigned f,bool s,bool safe,unsigned d,bool o) {
    if(!p)return features<C,Owners::Single>(f,s,safe,d,o);
    if(p==1)return features<C,Owners::Fixed>(f,s,safe,d,o);
    assert(p==2);return features<C,Owners::Shared>(f,s,safe,d,o);
}
int main(int argc,char** argv) {
    assert(argc==8);unsigned c=std::strtoul(argv[1],nullptr,10),p=std::strtoul(argv[2],nullptr,10),f=std::strtoul(argv[3],nullptr,10);
    bool s=std::strtoul(argv[4],nullptr,10)!=0,safe=std::strtoul(argv[5],nullptr,10)!=0;
    unsigned d=std::strtoul(argv[6],nullptr,10);bool opt=std::strtoul(argv[7],nullptr,10)!=0;assert(d<2);
    if(c==1)return policy<1>(p,f,s,safe,d,opt);
    if(c==2)return policy<2>(p,f,s,safe,d,opt);
    if(c==3)return policy<3>(p,f,s,safe,d,opt);
    assert(c==4);return policy<4>(p,f,s,safe,d,opt);
}
