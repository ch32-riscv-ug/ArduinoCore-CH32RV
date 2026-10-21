#pragma once
#include "h417_features.hpp"
#include "event_executor.hpp"

// N06 SERIAL connection experiment. Gate feedback below is call-local only
// because the fixture serializes all operations; it is NOT a concurrent HSEM
// adapter or a public result API. Existing Fabric/Executors stay unchanged.
namespace h417_executor_experiment {
using namespace h417_feature_experiment;
using namespace experiment;
struct SerialGate {
    bool blocked=false,held=false,entered=false;
    bool try_enter(){entered=false;if(blocked || held)return false;held=entered=true;return true;}
    void leave(){held=false;}
};
struct Ticket {uint32_t epoch=0;unsigned mask=0,destination=0;};
struct NoHook {static void after_probe(unsigned,unsigned){}};
template<bool Enabled> struct MessageState {};
template<> struct MessageState<true> {uint32_t messages=0,message_hash=2166136261u;};
template<bool Enabled> struct BlockState {};
template<> struct BlockState<true> {
    Token lease{};
    bool holding=false;
    uint8_t cursor=0;
    uint32_t hash=2166136261u,blocks=0,block_hash=2166136261u,bytes=0,invalidated=0;
};

template<unsigned Candidate,Owners Policy,unsigned Features,class Counter=uint32_t,
         bool ClearBeforeProbe=true,class Hook=NoHook> struct Application {
    static_assert(Candidate>=1 && Candidate<=4 && Features<4,"fixed comparisons");
    using Work=WorkExecutor<1>;
    using Events=EventExecutor<uint8_t,1,1>;
    struct Empty {};
    struct Worker : MessageState<(Features&1)!=0>,BlockState<(Features&2)!=0> {
        Application* app=nullptr;
        unsigned core=0,lane=0;
        bool online=true,opted=false;
        WorkDomain domain=WorkDomain::System;
        uint32_t calls=0,busy=0;
        typename std::conditional<Candidate==3,Work,typename std::conditional<Candidate==4,Events,Empty>::type>::type executor;
        typename std::conditional<Candidate==3,typename Work::Token,typename std::conditional<Candidate==4,typename Events::Token,Empty>::type>::type target{};
        bool pending() const {
            bool held=false;if constexpr(Features&2)held=this->holding;
            return app->hints[core] || held;
        }
        static void work(void* self,typename Work::Token){static_cast<Worker*>(self)->step();}
        static void event(void* self,const typename Events::Delivery&){static_cast<Worker*>(self)->step();}
        void begin(Application& owner,unsigned index,WorkDomain d,bool opt) {
            app=&owner;core=index;domain=d;opted=opt;
            if constexpr(Candidate==3){target=executor.add(work,this,d,opt);executor.initialize();}
            if constexpr(Candidate==4){target=executor.add(event,this,d,opt);executor.initialize();}
        }
        void messages_step() {
            if constexpr(Features&1) {
                if constexpr(ClearBeforeProbe)app->hints[core]&=uint8_t(~1u);
                Message message{};const bool ok=app->fabric.pop(core,message);
                const bool acquired=app->gate.entered; // serial call feedback
                Hook::after_probe(core,1);
                if constexpr(!ClearBeforeProbe)app->hints[core]&=uint8_t(~1u);
                if(ok) {
                    for(auto word:message.words)this->message_hash=(this->message_hash^word)*16777619u;
                    ++this->messages;app->hints[core]|=1;
                } else if(!acquired){++busy;app->hints[core]|=1;}
            }
        }
        void blocks_step() {
            if constexpr(Features&2) {
                if(this->holding && this->lease.epoch!=app->epoch) {
                    this->holding=false;++this->invalidated;this->cursor=0;return;
                }
                if(!this->holding) {
                    if constexpr(ClearBeforeProbe)app->hints[core]&=uint8_t(~2u);
                    Token token{};const bool ok=app->fabric.take(core,token);
                    const bool acquired=app->gate.entered;
                    Hook::after_probe(core,2);
                    if constexpr(!ClearBeforeProbe)app->hints[core]&=uint8_t(~2u);
                    if(ok){this->lease=token;this->holding=true;this->cursor=0;this->hash=2166136261u;app->hints[core]|=2;}
                    else if(!acquired){++busy;app->hints[core]|=2;}
                } else if(this->cursor<64) {
                    // At most eight separately gated bytes per ordinary call.
                    for(unsigned i=0;i<8 && this->cursor<64;++i) {
                        unsigned value=0;
                        if(!app->fabric.read(core,this->lease,this->cursor,value)) {
                            if(!app->gate.entered)++busy;
                            else {this->holding=false;++this->invalidated;}
                            break;
                        }
                        this->hash=(this->hash^value)*16777619u;++this->cursor;++this->bytes;
                    }
                } else if(app->fabric.release(core,this->lease)) {
                    this->holding=false;++this->blocks;
                    this->block_hash=(this->block_hash^this->hash)*16777619u;
                } else if(!app->gate.entered)++busy;
                else {this->holding=false;++this->invalidated;}
            }
        }
        void step() {
            if(!online || app->recovering || !Application::consumer(core))return;
            ++calls;
            bool block=false;if constexpr(Features&2)block=this->holding || (app->hints[core]&2);
            const bool message=(Features&1) && (app->hints[core]&1);
            if(message && (!block || lane==0)){messages_step();lane=1;}
            else if(block){blocks_step();lane=0;}
        }
        TurnStatus opportunity(WorkOpportunity where,WorkProfile profile,WorkContext context) {
            if(!online || app->recovering || !Application::consumer(core))return TurnStatus::NotInitialized;
            if constexpr(Candidate>=3) {
                if(context==WorkContext::Normal && pending()) {
                    if constexpr(Candidate==3)executor.post(target);else executor.notify(target,1);
                }
                return executor.turn(1,where,profile,context).status;
            } else {
                if(context==WorkContext::Isr || context==WorkContext::Setup)return TurnStatus::Forbidden;
                if(context==WorkContext::Callback)return TurnStatus::Reentrant;
                if(where!=WorkOpportunity::Explicit) {
                    if(profile==WorkProfile::M)return TurnStatus::Suppressed;
                    if(domain==WorkDomain::User && (profile!=WorkProfile::U || !opted))return TurnStatus::Ran;
                }
                if(pending())step();
                return TurnStatus::Ran;
            }
        }
    };
    SerialGate gate;
    Fabric<Policy,SerialGate,(Features&1)!=0,(Features&2)!=0,Counter> fabric;
    Worker workers[2];
    Counter epoch=1;
    uint8_t hints[2]{};
    bool recovering=false,dma_quiet=false;
    constexpr Application():fabric(gate,9){}
    static bool producer(unsigned core){return core<2 && (Policy==Owners::Shared || core==0);}
    static bool consumer(unsigned core){return core<2 && (Policy==Owners::Shared || core==(Policy==Owners::Single?0u:1u));}
    void begin(WorkDomain domain=WorkDomain::System,bool opt=false){for(unsigned i=0;i<2;++i)workers[i].begin(*this,i,domain,opt);}
    bool hint(Ticket t) {
        if(recovering || t.epoch!=epoch || !t.mask || (t.mask&~Features) || !consumer(t.destination))return false;
        hints[t.destination]|=uint8_t(t.mask);return true;
    }
    bool push(unsigned core,unsigned destination,uint32_t value,Ticket& out) {
        if(recovering || !consumer(destination))return false;
        const Message m{{value,value+1,value+2,value+3}};
        if(!fabric.push(core,m))return false;
        out=Ticket{epoch,1,destination};return true;
    }
    bool reserve(unsigned core,Token& out){return !recovering && fabric.reserve(core,out);}
    bool write(unsigned core,Token t,unsigned index,unsigned value){return !recovering && fabric.write(core,t,index,value);}
    bool publish(unsigned core,Token t,unsigned destination,Ticket& out) {
        if(recovering || !consumer(destination) || !fabric.publish(core,t))return false;
        out=Ticket{epoch,2,destination};return true;
    }
    bool recover(){if(!fabric.begin_recovery(0))return false;recovering=true;dma_quiet=false;return true;}
    bool attest(unsigned mask) {
        if(!recovering || !mask || mask>7 || ((mask&4) && !dma_quiet))return false;
        for(unsigned core=0;core<2;++core) {
            if(((mask&1) && producer(core)) || ((mask&2) && consumer(core)))
                if(workers[core].online)return false;
        }
        return fabric.attest(0,mask);
    }
    bool restart() {
        if(!fabric.restart(0))return false;
        ++epoch;hints[0]=hints[1]=0;recovering=false;return true;
    }
};
} // namespace h417_executor_experiment
