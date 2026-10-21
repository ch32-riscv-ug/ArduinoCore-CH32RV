#include "h417_endpoint.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using namespace h417_transport_experiment;
struct Tracking {
    static inline thread_local uint64_t ticket=0;
    uint64_t clock=0;
    std::atomic<bool> blocked{false};
    std::atomic<unsigned> attempts{0},busy{0};
    bool rejected(){attempts.fetch_add(1,std::memory_order_relaxed);return blocked.load(std::memory_order_relaxed);}
    bool fail(){busy.fetch_add(1,std::memory_order_relaxed);return false;}
    void entered(){ticket=++clock;}
};
struct MutexGate:Tracking {
    std::mutex mutex;
    bool try_enter(){if(rejected() || !mutex.try_lock())return fail();entered();return true;}
    void leave(){mutex.unlock();}
};
struct AtomicGate:Tracking {
    std::atomic_flag flag=ATOMIC_FLAG_INIT;
    bool try_enter(){if(rejected() || flag.test_and_set(std::memory_order_acquire))return fail();entered();return true;}
    void leave(){flag.clear(std::memory_order_release);}
};
template<class Function> Result retry(Function function) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    for(;;) {
        const Result result=function();if(result!=Result::Busy)return result;
        assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();
    }
}
struct Record {uint64_t ticket;uint32_t value,hash;};
struct Hook {
    static inline std::array<std::vector<Record>,2> messages,blocks;
    static inline uint64_t take_ticket[2]{};
    static inline std::function<void(unsigned,unsigned)> control;
    static void point(unsigned core,unsigned stage){if(control)control(core,stage);}
    static void message(unsigned core,const Message& message) {
        for(unsigned i=0;i<4;++i)assert(message.words[i]==message.words[0]+i);
        messages[core].push_back({Tracking::ticket,message.words[0],0});
    }
    static void take(unsigned core,Token){take_ticket[core]=Tracking::ticket;}
    static void block(unsigned core,Token token,uint32_t hash){blocks[core].push_back({take_ticket[core],token.generation,hash});}
};
struct Snapshot {
    unsigned session_values[8]{},fabric_values[8]{};
    unsigned phase[2]{},count=0;
    void session(unsigned e,bool r,unsigned enabled,unsigned active,unsigned quiet,bool dma,unsigned h0,unsigned h1){
        const unsigned values[]={e,unsigned(r),enabled,active,quiet,unsigned(dma),h0,h1};
        std::copy(values,values+8,session_values);
    }
    void start(unsigned d,unsigned e,unsigned g,bool r,unsigned q,unsigned n,unsigned m,unsigned b){
        const unsigned values[]={d,e,g,unsigned(r),q,n,m,b};std::copy(values,values+8,fabric_values);
    }
    void message(const Message&){}
    void blocks(){}
    void block(unsigned p,unsigned,unsigned,unsigned,uint32_t){assert(count<2);phase[count++]=p;}
    void end(){}
};
template<class Transport> Snapshot snapshot(Transport& view){Snapshot out;assert(retry([&]{return view.observe(out);})==Result::Done);return out;}
void output_array(const unsigned* values,unsigned n){std::cout<<'[';for(unsigned i=0;i<n;++i)std::cout<<(i?",":"")<<values[i];std::cout<<']';}
void output_snapshot(const Snapshot& s){std::cout<<"\"session\":";output_array(s.session_values,8);std::cout<<",\"fabric\":";output_array(s.fabric_values,8);std::cout<<",\"phases\":";output_array(s.phase,s.count);}
void output_records(const std::array<std::vector<Record>,2>& records) {
    std::cout<<'[';
    for(unsigned core=0;core<2;++core){std::cout<<(core?",":"")<<'[';unsigned i=0;for(auto r:records[core])std::cout<<(i++?",":"")<<'['<<r.ticket<<','<<r.value<<','<<r.hash<<']';std::cout<<']';}
    std::cout<<']';
}
template<class Ep> void output_endpoint(const Ep& ep) {
    std::cout<<'['<<ep.borrowed<<','<<ep.calls<<','<<unsigned(ep.last_data);
    if constexpr(FEATURES&1)std::cout<<','<<ep.messages<<','<<ep.message_hash;else std::cout<<",0,0";
    if constexpr(FEATURES&2)std::cout<<','<<ep.holding<<','<<unsigned(ep.cursor)<<','<<ep.bytes<<','<<ep.blocks<<','<<ep.block_hash<<','<<ep.invalidated;
    else std::cout<<",0,0,0,0,0,0";
    std::cout<<']';
}
template<class View> void source_block(View& view,unsigned core,unsigned destination,unsigned seed,Token& token) {
    assert(retry([&]{return view.reserve(core,token);})==Result::Done);
    for(unsigned i=0;i<64;++i)assert(retry([&]{return view.write(core,token,i,uint8_t(seed+i));})==Result::Done);
    Ticket ticket{};assert(retry([&]{return view.publish(core,token,destination,ticket);})==Result::Done);
    assert(retry([&]{return view.hint(ticket);})==Result::Done);
    if constexpr(View::StateType::policy==Owners::Shared){ticket.destination=destination^1;assert(retry([&]{return view.hint(ticket);})==Result::Done);}
}
template<unsigned C,Owners P,class Gate> int stress(unsigned seed,unsigned count) {
    using State=Shared<P,FEATURES>;using Transport=View<State,Gate>;using Ep=Endpoint<C,Transport,Hook>;
    State state;Gate gate;Transport views[2]={{state,gate},{state,gate}};Ep endpoints[2];
    for(unsigned i=0;i<2;++i)assert(endpoints[i].begin(views[i],i));
    const unsigned consumer=P==Owners::Single?0:1,block_count=(FEATURES&2)?count/4:0,message_count=(FEATURES&1)?count:0;
    std::atomic<bool> done{false};
    auto producer=[&]{
        for(unsigned i=0;i<count;++i) {
            if constexpr(FEATURES&1) {
                Ticket ticket{};Result r;
                do {r=retry([&]{return views[0].push(0,consumer,seed+i,ticket);});if(r==Result::Full)std::this_thread::yield();}while(r==Result::Full);
                assert(r==Result::Done);assert(retry([&]{return views[0].hint(ticket);})==Result::Done);
                if constexpr(P==Owners::Shared){ticket.destination=consumer^1;assert(retry([&]{return views[0].hint(ticket);})==Result::Done);}
            }
            if constexpr(FEATURES&2)if(i%4==0 && i/4<block_count) {
                Token token{};Result r;
                do {r=retry([&]{return views[0].reserve(0,token);});if(r==Result::Rejected)std::this_thread::yield();}while(r==Result::Rejected);
                assert(r==Result::Done);
                for(unsigned b=0;b<64;++b)assert(retry([&]{return views[0].write(0,token,b,uint8_t(seed+i/4+b));})==Result::Done);
                Ticket ticket{};assert(retry([&]{return views[0].publish(0,token,consumer,ticket);})==Result::Done);
                assert(retry([&]{return views[0].hint(ticket);})==Result::Done);
                if constexpr(P==Owners::Shared){ticket.destination=consumer^1;assert(retry([&]{return views[0].hint(ticket);})==Result::Done);}
            }
        }
        done.store(true,std::memory_order_release);
    };
    auto consume=[&](unsigned core){
        auto& ep=endpoints[core];unsigned idle=0;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(idle<32 || ep.borrowed) {
            auto result=ep.opportunity(WorkOpportunity::Explicit,WorkProfile::M);
            if(done.load(std::memory_order_acquire) && !result.delivered && result.transport==Result::Done)++idle;else idle=0;
            assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();
        }
    };
    std::thread source(producer),first(consume,consumer);std::thread second;
    if constexpr(P==Owners::Shared)second=std::thread(consume,consumer^1);
    source.join();first.join();if(second.joinable())second.join();
    auto snap=snapshot(views[0]);
    assert(!snap.session_values[3] && !snap.fabric_values[6] && !snap.fabric_values[7]);
    for(unsigned i=0;i<snap.count;++i)assert(snap.phase[i]==0);
    auto validate=[&](const auto& records,unsigned expected,bool message){
        auto merged=records[0];merged.insert(merged.end(),records[1].begin(),records[1].end());
        std::sort(merged.begin(),merged.end(),[](auto a,auto b){return a.ticket<b.ticket;});assert(merged.size()==expected);
        for(unsigned i=0;i<merged.size();++i)assert(merged[i].value==(message?seed+i:i+1));
    };
    validate(Hook::messages,message_count,true);validate(Hook::blocks,block_count,false);
    std::cout<<'{';output_snapshot(snap);std::cout<<",\"endpoints\":[";output_endpoint(endpoints[0]);std::cout<<',';output_endpoint(endpoints[1]);
    std::cout<<"],\"messages\":";output_records(Hook::messages);std::cout<<",\"blocks\":";output_records(Hook::blocks);
    std::cout<<",\"attempts\":"<<gate.attempts.load()<<",\"busy\":"<<gate.busy.load()<<"}\n";return 0;
}
template<unsigned C,class Gate> int races(bool cleanup) {
    using State=Shared<Owners::Fixed,FEATURES>;using Transport=View<State,Gate>;using Ep=Endpoint<C,Transport,Hook>;
    State state;Gate gate;Transport source(state,gate),consumer(state,gate);Ep endpoint;assert(endpoint.begin(consumer,1));
    Token old{};Ticket delayed{};
    if(!cleanup) {
        if constexpr(FEATURES&1){assert(source.push(0,1,10,delayed)==Result::Done);assert(source.hint(delayed)==Result::Done);}
        if constexpr(FEATURES&2)source_block(source,0,1,20,old);
    }
    if constexpr(FEATURES==2)if(!cleanup)endpoint.opportunity(WorkOpportunity::Explicit,WorkProfile::M);
    unsigned before_active=0;PumpResult first{};
    if(cleanup) {
        Hook::control=[&](unsigned,unsigned stage){if(stage==5)gate.blocked.store(true);};
        first=endpoint.opportunity(WorkOpportunity::Explicit,WorkProfile::M);Hook::control=nullptr;
        assert(first.transport==Result::Busy && endpoint.borrowed);gate.blocked.store(false);
    } else {
        std::mutex pause;std::condition_variable changed;bool reached=false,proceed=false;
        const unsigned stage=(FEATURES&1)?3:((FEATURES&2)?4:1);
        Hook::control=[&](unsigned,unsigned point){
            if(point!=stage)return;
            std::unique_lock<std::mutex> lock(pause);if(reached)return;reached=true;changed.notify_one();changed.wait(lock,[&]{return proceed;});
        };
        std::thread worker([&]{first=endpoint.opportunity(WorkOpportunity::Explicit,WorkProfile::M);while(endpoint.borrowed)endpoint.opportunity(WorkOpportunity::Explicit,WorkProfile::M);});
        {std::unique_lock<std::mutex> lock(pause);assert(changed.wait_for(lock,std::chrono::seconds(20),[&]{return reached;}));}
        assert(source.recover()==Result::Done);assert(source.enable(0,false)==Result::Done && source.enable(1,false)==Result::Done);assert(source.dma(true)==Result::Done);
        assert(source.attest(7)==Result::Busy && source.restart()==Result::Busy);
        before_active=snapshot(source).session_values[3];assert(before_active==2);
        {std::lock_guard<std::mutex> lock(pause);proceed=true;}changed.notify_one();worker.join();Hook::control=nullptr;
    }
    if(cleanup) {
        assert(source.recover()==Result::Done);assert(source.enable(0,false)==Result::Done && source.enable(1,false)==Result::Done);assert(source.dma(true)==Result::Done);
        assert(source.attest(7)==Result::Busy && source.restart()==Result::Busy);before_active=snapshot(source).session_values[3];
        const auto forbidden=endpoint.opportunity(WorkOpportunity::Explicit,WorkProfile::M,WorkContext::Isr);assert(forbidden.context==TurnStatus::Forbidden && endpoint.borrowed);
        const auto result=endpoint.opportunity(WorkOpportunity::Explicit,WorkProfile::M);assert(!result.delivered && result.transport==Result::Done && !endpoint.borrowed);
    }
    assert(source.attest(7)==Result::Done && source.restart()==Result::Done);
    if constexpr(FEATURES&1)if(!cleanup)assert(source.hint(delayed)==Result::Stale);
    if constexpr(FEATURES&2)if(!cleanup)assert(source.write(0,old,0,99)==Result::Stale);
    assert(source.enable(0,true)==Result::Done && source.enable(1,true)==Result::Done);
    if constexpr(FEATURES&1){Ticket t{};assert(source.push(0,1,77,t)==Result::Done && source.hint(t)==Result::Done);}
    if constexpr(FEATURES&2){Token t{};source_block(source,0,1,90,t);}
    for(unsigned i=0;i<32;++i)endpoint.opportunity(WorkOpportunity::Explicit,WorkProfile::M);
    std::cout<<'{';output_snapshot(snapshot(source));std::cout<<",\"before_active\":"<<before_active<<",\"cleanup\":"<<cleanup<<",\"endpoint\":";output_endpoint(endpoint);std::cout<<"}\n";return 0;
}
template<unsigned C,class Gate> int select(const std::string& mode,unsigned policy,unsigned seed,unsigned count) {
    if(mode=="races")return races<C,Gate>(false);
    if(mode=="cleanup")return races<C,Gate>(true);
    assert(mode=="stress");
    if(policy==0)return stress<C,Owners::Single,Gate>(seed,count);
    if(policy==1)return stress<C,Owners::Fixed,Gate>(seed,count);
    assert(policy==2);return stress<C,Owners::Shared,Gate>(seed,count);
}
template<class Gate> int candidate(unsigned c,const std::string& mode,unsigned p,unsigned s,unsigned n) {
    if(c==1)return select<1,Gate>(mode,p,s,n);
    if(c==2)return select<2,Gate>(mode,p,s,n);
    if(c==3)return select<3,Gate>(mode,p,s,n);
    assert(c==4);return select<4,Gate>(mode,p,s,n);
}
int main(int argc,char** argv){
    assert(argc==7);const std::string mode=argv[1],gate=argv[2];unsigned c=std::strtoul(argv[3],nullptr,10),p=std::strtoul(argv[4],nullptr,10),s=std::strtoul(argv[5],nullptr,10),n=std::strtoul(argv[6],nullptr,10);
    if(gate=="mutex")return candidate<MutexGate>(c,mode,p,s,n);
    assert(gate=="atomic");return candidate<AtomicGate>(c,mode,p,s,n);
}
