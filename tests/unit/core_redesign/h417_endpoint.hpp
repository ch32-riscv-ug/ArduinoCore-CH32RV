#pragma once
#include "h417_transport.hpp"
#include "event_executor.hpp"

// One normal caller per Endpoint. Other cores use their own Endpoint/View.
// Closing a turn is explicit retryable work; object lifetime includes cleanup.
namespace h417_transport_experiment {
using namespace experiment;
struct NoHook {
    static void point(unsigned,unsigned){}
    static void message(unsigned,const Message&){}
    static void take(unsigned,Token){}
    static void block(unsigned,Token,uint32_t){}
};
template<bool Enabled> struct MessageProgress {};
template<> struct MessageProgress<true> {uint32_t messages=0,message_hash=2166136261u;};
template<bool Enabled> struct BlockProgress {};
template<> struct BlockProgress<true> {
    Token lease{};bool holding=false;uint8_t cursor=0;
    uint32_t bytes=0,blocks=0,hash=2166136261u,block_hash=2166136261u,invalidated=0;
};
struct PumpResult {TurnStatus context;Result transport;bool delivered,cleanup_pending;};
template<unsigned Candidate,class Transport,class Hook=NoHook> struct Endpoint
    : MessageProgress<(Transport::StateType::features&1)!=0>,BlockProgress<(Transport::StateType::features&2)!=0> {
    static_assert(Candidate>=1 && Candidate<=4,"four strategy comparisons");
    Endpoint()=default;
    Endpoint(const Endpoint&)=delete;Endpoint& operator=(const Endpoint&)=delete;
    Endpoint(Endpoint&&)=delete;Endpoint& operator=(Endpoint&&)=delete;
    static constexpr unsigned features=Transport::StateType::features;
    using Work=WorkExecutor<1>;using Events=EventExecutor<uint8_t,1,1>;
    struct Empty {};
    Transport* view=nullptr;
    unsigned core=2,lane=0,current_hints=0;
    WorkDomain domain=WorkDomain::System;
    bool opted=false,borrowed=false,in_turn=false;
    Permit permit{};
    uint32_t calls=0;
    Result last_data=Result::Empty;
    typename std::conditional<Candidate==3,Work,typename std::conditional<Candidate==4,Events,Empty>::type>::type executor;
    typename std::conditional<Candidate==3,typename Work::Token,typename std::conditional<Candidate==4,typename Events::Token,Empty>::type>::type target{};
    static void work(void* self,typename Work::Token){static_cast<Endpoint*>(self)->step();}
    static void event(void* self,const typename Events::Delivery&){static_cast<Endpoint*>(self)->step();}
    bool begin(Transport& transport,unsigned index,WorkDomain d=WorkDomain::System,bool opt=false) {
        if(view || index>=2)return false;
        view=&transport;core=index;domain=d;opted=opt;
        if constexpr(Candidate==3){target=executor.add(work,this,d,opt);executor.initialize();}
        if constexpr(Candidate==4){target=executor.add(event,this,d,opt);executor.initialize();}
        return true;
    }
    void message_step() {
        if constexpr(features&1) {
            Message message{};last_data=view->pop(permit,message);Hook::point(core,3);
            if(last_data==Result::Done) {
                for(auto value:message.words)this->message_hash=(this->message_hash^value)*16777619u;
                ++this->messages;Hook::message(core,message);
            }
        }
    }
    void block_step() {
        if constexpr(features&2) {
            if(this->holding && this->lease.epoch!=permit.epoch) {
                this->holding=false;this->cursor=0;++this->invalidated;last_data=Result::Stale;return;
            }
            if(!this->holding) {
                Token t{};last_data=view->take(permit,t);
                if(last_data==Result::Done){this->lease=t;this->holding=true;this->cursor=0;this->hash=2166136261u;Hook::take(core,t);}
            } else if(this->cursor<64) {
                for(unsigned i=0;i<8 && this->cursor<64;++i) {
                    unsigned value=0;last_data=view->read(permit,this->lease,this->cursor,value);
                    if(last_data!=Result::Done)break;
                    this->hash=(this->hash^value)*16777619u;++this->cursor;++this->bytes;Hook::point(core,4);
                }
                if(last_data==Result::Stale || last_data==Result::Rejected){this->holding=false;++this->invalidated;}
            } else {
                last_data=view->release(permit,this->lease);
                if(last_data==Result::Done){this->holding=false;++this->blocks;this->block_hash=(this->block_hash^this->hash)*16777619u;Hook::block(core,this->lease,this->hash);}
                else if(last_data==Result::Stale || last_data==Result::Rejected){this->holding=false;++this->invalidated;}
            }
        }
    }
    void step() {
        ++calls;
        bool block=false;if constexpr(features&2)block=this->holding || (current_hints&2);
        const bool message=(features&1) && (current_hints&1);
        if(message && (!block || lane==0)){message_step();lane=1;}
        else if(block){block_step();lane=0;}
    }
    PumpResult opportunity(WorkOpportunity where,WorkProfile profile,WorkContext context=WorkContext::Normal) {
        if(!view)return {TurnStatus::NotInitialized,Result::Closed,false,false};
        if(context==WorkContext::Isr || context==WorkContext::Setup)return {TurnStatus::Forbidden,Result::Rejected,false,borrowed};
        if(in_turn || context==WorkContext::Callback)return {TurnStatus::Reentrant,Result::Rejected,false,borrowed};
        in_turn=true;
        struct Reset {bool& flag;~Reset(){flag=false;}} reset{in_turn};
        // Cleanup never runs a user handler; keep an acquired permit live when
        // Gate is busy, even if recovery closed new work in the meantime.
        if(borrowed) {
            const Result result=view->close_turn(permit);
            if(result==Result::Done)borrowed=false;
            return {TurnStatus::Ran,result,false,borrowed};
        }
        if(where!=WorkOpportunity::Explicit) {
            if(profile==WorkProfile::M)return {TurnStatus::Suppressed,Result::Done,false,false};
            if(domain==WorkDomain::User && (profile!=WorkProfile::U || !opted))return {TurnStatus::Ran,Result::Done,false,false};
        }
        TurnInput input{};const Result acquired=view->open_turn(core,input);
        if(acquired!=Result::Done)return {TurnStatus::NotInitialized,acquired,false,false};
        permit=input.permit;current_hints=input.hints;borrowed=true;Hook::point(core,1);
        bool held=false;if constexpr(features&2)held=this->holding;
        const bool pending=current_hints || held;
        if(pending) {
            if constexpr(Candidate==3){executor.post(target);executor.turn(1,where,profile,context);}
            else if constexpr(Candidate==4){executor.notify(target,1);executor.turn(1,where,profile,context);}
            else step();
        }
        Hook::point(core,5);const Result closed=view->close_turn(permit);
        if(closed==Result::Done)borrowed=false;
        return {TurnStatus::Ran,closed,pending,borrowed};
    }
};
} // namespace h417_transport_experiment
