#pragma once
#include "sleep_coordinator.hpp"
#include "event_executor.hpp"

// N06 serial composition only. No physical wake gate, IRQ posting, WFI, or
// callback during sleep/clock restore. Existing components stay unchanged.
namespace sleep_work_experiment {
using namespace sleep_experiment;
using namespace experiment;
template<unsigned Candidate,bool CheckJobs=true> struct Worker {
    static_assert(Candidate>=1 && Candidate<=4,"four experimental strategies");
    using Work=WorkExecutor<1>;
    using Event=EventExecutor<uint8_t,1,1>;
    struct Empty {};
    using Executor=typename std::conditional<Candidate==3,Work,typename std::conditional<Candidate==4,Event,Empty>::type>::type;
    using Target=typename std::conditional<Candidate==3,typename Work::Token,typename std::conditional<Candidate==4,typename Event::Token,Empty>::type>::type;
    Port<16> port;
    Executor executor;
    Target target{};
    WorkDomain domain=WorkDomain::System;
    bool opted=false;
    uint8_t remaining=0;
    uint32_t completed_steps=0,collected=0,calls=0;
    IoSlot<16>::Completion completion;
    void step() {
        ++calls;
        if(port.io.terminal()) {
            if(port.io.take(port.io.token(),completion))++collected;
        } else if(remaining) {--remaining;++completed_steps;}
    }
    static void work(void* self,typename Work::Token){static_cast<Worker*>(self)->step();}
    static void event(void* self,const typename Event::Delivery&){static_cast<Worker*>(self)->step();}
    bool begin(WorkDomain d,bool opt) {
        domain=d;opted=opt;
        if(!port.init())return false;
        if constexpr(Candidate==3) {target=executor.add(work,this,d,opt);executor.initialize();return executor.valid(target);}
        if constexpr(Candidate==4) {target=executor.add(event,this,d,opt);executor.initialize();return executor.valid(target);}
        return true;
    }
    bool submit(unsigned count) {
        if(!port.powered || !port.admitting || remaining || !count || count>255)return false;
        remaining=uint8_t(count);return true;
    }
    bool ready() const {return remaining || port.io.terminal();}
    Reply sleep_step(Action action) {
        const Reply result=port.step(action);
        if constexpr(CheckJobs) {
            if((action==Action::Prepare || action==Action::Quiet) && remaining)
                return combine(result,Reply::Waiting);
            if(action==Action::Off && remaining)return Reply::Failed;
        }
        return result;
    }
    // Participant callback has a separate name from ordinary job progress.
    Reply step(Action action){return sleep_step(action);}
    static Reply callback(void* self,Action action){return static_cast<Worker*>(self)->sleep_step(action);}
    TurnStatus opportunity(WorkOpportunity where,WorkProfile profile,WorkContext context) {
        if constexpr(Candidate>=3) {
            // Hint publication is explicit normal-context work, not ISR posting.
            if(context==WorkContext::Normal && ready()) {
                if constexpr(Candidate==3)executor.post(target);
                else executor.notify(target,1);
            }
            return executor.turn(1,where,profile,context).status;
        } else {
            if(context==WorkContext::Isr || context==WorkContext::Setup)return TurnStatus::Forbidden;
            if(context==WorkContext::Callback)return TurnStatus::Reentrant;
            if(where!=WorkOpportunity::Explicit) {
                if(profile==WorkProfile::M)return TurnStatus::Suppressed;
                if(domain==WorkDomain::User && (profile!=WorkProfile::U || !opted))return TurnStatus::Ran;
            }
            if(ready())step();
            return TurnStatus::Ran;
        }
    }
};

template<bool Timed> struct DeadlineStorage {};
template<> struct DeadlineStorage<true> { Timer timer; };
static_assert(std::is_empty<DeadlineStorage<false>>::value,"disabled timer owns no member");

template<unsigned Candidate,unsigned Capacity,bool Timed,bool CheckJobs=true> struct Application : DeadlineStorage<Timed> {
    static_assert(Capacity==0 || Capacity==2 || Capacity==4,"fixed list comparisons");
    using Actor=Worker<Candidate,CheckJobs>;
    using List=typename std::conditional<Capacity==0,Explicit<Actor,Actor>,Registry<Capacity?Capacity:1>>::type;
    using Deadline=typename std::conditional<Timed,Timer,NoDeadline>::type;
    Actor workers[2];
    List list;
    Clock clock;
    Coordinator<List,uint32_t,Deadline> flow;
    time_experiment::Stamp stamp{};
    time_experiment::Notice notice{};
    time_experiment::Result source_status=time_experiment::Result::Closed;
    uint32_t attempt=0;
    bool begin(WorkDomain domain,bool opt) {
        source_status=clock.begin({0,1,6000000},0,stamp);
        if(!time_experiment::ready(source_status))return false;
        for(auto& worker:workers)if(!worker.begin(domain,opt))return false;
        if constexpr(Capacity==0) {if(!list.bind(workers[0],workers[1]))return false;}
        else {typename List::Token token{};for(auto& worker:workers)if(!list.add(&worker,&Actor::callback,token))return false;}
        if constexpr(Timed)return flow.bind(list,clock,this->timer);
        else return flow.bind(list,clock);
    }
    // Callers provide explicit opportunities. A scheduler does not invoke this
    // from Prepare/Quiet/Off/Restore/Activate. Clock and powered gates apply to
    // recovery too; a partially restored port can never start accepting early.
    TurnStatus opportunity(unsigned side,WorkOpportunity where,WorkProfile profile,WorkContext context) {
        if(side>=2 || !flow.clock_ready() || !workers[side].port.powered)return TurnStatus::NotInitialized;
        return workers[side].opportunity(where,profile,context);
    }
};
} // namespace sleep_work_experiment
