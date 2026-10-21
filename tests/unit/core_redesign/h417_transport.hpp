#pragma once
#include "h417_features.hpp"
#include <limits>
#include <type_traits>

// N06 comparison. Gate supplies cross-core exclusion AND visibility. No MCU
// gate/cache backend here. Shared holds transport only; View is core-local.
namespace h417_transport_experiment {
using namespace h417_feature_experiment;
enum class Result : uint8_t { Done,Empty,Busy,Closed,Stale,Rejected,Unsupported,Exhausted,Full };
struct Ticket {uint32_t epoch=0;unsigned mask=0,destination=0;};
struct Permit {uint32_t epoch=0,serial=0;unsigned core=2;};
struct TurnInput {Permit permit{};unsigned hints=0;};
struct Ungated {
    // All Fabric calls are private to View and already under the outer Gate.
    static constexpr bool try_enter(){return true;}
    static void leave(){}
};
template<Owners Policy,unsigned Features,class Counter=uint32_t> class Shared {
    static_assert(Features<4,"four feature projections");
    template<class,class> friend class View;
    Ungated inner;
    Fabric<Policy,Ungated,(Features&1)!=0,(Features&2)!=0,Counter> fabric;
    Counter epoch=1;
    uint32_t serial[2]{};
    uint8_t hints[2]{},enabled=3,active=0,quiet=0;
    bool recovering=false,dma_quiet=false;
public:
    static constexpr unsigned features=Features;
    static constexpr Owners policy=Policy;
    constexpr Shared():fabric(inner,9){}
    Shared(const Shared&)=delete;
    Shared& operator=(const Shared&)=delete;
    static bool producer(unsigned core){return core<2 && (Policy==Owners::Shared || core==0);}
    static bool consumer(unsigned core){return core<2 && (Policy==Owners::Shared || core==(Policy==Owners::Single?0u:1u));}
};

template<class State,class Gate> class View {
    State& state;
    Gate& gate;
    struct Guard {
        Gate& gate;bool held;
        explicit Guard(Gate& g):gate(g),held(g.try_enter()){}
        ~Guard(){if(held)gate.leave();}
    };
    bool matches(Permit p) const {
        return p.core<2 && p.epoch==state.epoch && p.serial &&
               (state.active&(1u<<p.core)) && state.serial[p.core]==p.serial;
    }
    Result permission(Permit p) const {
        if(!matches(p))return Result::Stale;
        return state.recovering?Result::Closed:Result::Done;
    }
public:
    using StateType=State;
    constexpr View(State& s,Gate& g):state(s),gate(g){}
    Result open_turn(unsigned core,TurnInput& out) {
        Guard guard(gate);if(!guard.held)return Result::Busy;
        if(!State::consumer(core))return Result::Rejected;
        if(state.recovering || !(state.enabled&(1u<<core)))return Result::Closed;
        if(state.active&(1u<<core))return Result::Busy;
        if(state.serial[core]==UINT32_MAX)return Result::Exhausted;
        ++state.serial[core];state.active|=uint8_t(1u<<core);
        out={{state.epoch,state.serial[core],core},state.hints[core]};return Result::Done;
    }
    Result close_turn(Permit p) {
        Guard guard(gate);if(!guard.held)return Result::Busy;
        if(!matches(p))return Result::Stale;
        state.active&=uint8_t(~(1u<<p.core));return Result::Done;
    }
    Result push(unsigned core,unsigned destination,uint32_t value,Ticket& out) {
        if constexpr(!(State::features&1))return Result::Unsupported;
        else {
            Guard guard(gate);if(!guard.held)return Result::Busy;
            if(state.recovering)return Result::Closed;
            if(!State::producer(core) || !State::consumer(destination))return Result::Rejected;
            const Message message{{value,value+1,value+2,value+3}};
            if(!state.fabric.push(core,message))return Result::Full;
            out={state.epoch,1,destination};return Result::Done;
        }
    }
    Result reserve(unsigned core,Token& out) {
        if constexpr(!(State::features&2))return Result::Unsupported;
        else {
            Guard guard(gate);if(!guard.held)return Result::Busy;
            if(state.recovering)return Result::Closed;
            if(!State::producer(core))return Result::Rejected;
            return state.fabric.reserve(core,out)?Result::Done:Result::Rejected;
        }
    }
    Result write(unsigned core,Token t,unsigned index,unsigned value) {
        if constexpr(!(State::features&2))return Result::Unsupported;
        else {
            Guard guard(gate);if(!guard.held)return Result::Busy;
            if(state.recovering)return Result::Closed;
            if(t.epoch!=state.epoch)return Result::Stale;
            return state.fabric.write(core,t,index,value)?Result::Done:Result::Rejected;
        }
    }
    Result publish(unsigned core,Token t,unsigned destination,Ticket& out) {
        if constexpr(!(State::features&2))return Result::Unsupported;
        else {
            Guard guard(gate);if(!guard.held)return Result::Busy;
            if(state.recovering)return Result::Closed;
            if(t.epoch!=state.epoch)return Result::Stale;
            if(!State::consumer(destination) || !state.fabric.publish(core,t))return Result::Rejected;
            out={state.epoch,2,destination};return Result::Done;
        }
    }
    Result hint(Ticket t) {
        Guard guard(gate);if(!guard.held)return Result::Busy;
        if(state.recovering)return Result::Closed;
        if(t.epoch!=state.epoch)return Result::Stale;
        if(!t.mask || (t.mask&~State::features) || !State::consumer(t.destination))return Result::Rejected;
        state.hints[t.destination]|=uint8_t(t.mask);return Result::Done;
    }
    Result pop(Permit p,Message& out) {
        if constexpr(!(State::features&1))return Result::Unsupported;
        else {
            Guard guard(gate);if(!guard.held)return Result::Busy;
            const Result allowed=permission(p);if(allowed!=Result::Done)return allowed;
            state.hints[p.core]&=uint8_t(~1u);
            if(!state.fabric.pop(p.core,out))return Result::Empty;
            state.hints[p.core]|=1;return Result::Done;
        }
    }
    Result take(Permit p,Token& out) {
        if constexpr(!(State::features&2))return Result::Unsupported;
        else {
            Guard guard(gate);if(!guard.held)return Result::Busy;
            const Result allowed=permission(p);if(allowed!=Result::Done)return allowed;
            state.hints[p.core]&=uint8_t(~2u);
            if(!state.fabric.take(p.core,out))return Result::Empty;
            state.hints[p.core]|=2;return Result::Done;
        }
    }
    Result read(Permit p,Token t,unsigned index,unsigned& out) {
        if constexpr(!(State::features&2))return Result::Unsupported;
        else {
            Guard guard(gate);if(!guard.held)return Result::Busy;
            const Result allowed=permission(p);if(allowed!=Result::Done)return allowed;
            if(t.epoch!=state.epoch)return Result::Stale;
            return state.fabric.read(p.core,t,index,out)?Result::Done:Result::Rejected;
        }
    }
    Result release(Permit p,Token t) {
        if constexpr(!(State::features&2))return Result::Unsupported;
        else {
            Guard guard(gate);if(!guard.held)return Result::Busy;
            const Result allowed=permission(p);if(allowed!=Result::Done)return allowed;
            if(t.epoch!=state.epoch)return Result::Stale;
            return state.fabric.release(p.core,t)?Result::Done:Result::Rejected;
        }
    }
    Result enable(unsigned core,bool online) {
        Guard guard(gate);if(!guard.held)return Result::Busy;
        if(core>=2)return Result::Rejected;
        if(online)state.enabled|=uint8_t(1u<<core);else state.enabled&=uint8_t(~(1u<<core));
        return Result::Done;
    }
    Result recover() {
        Guard guard(gate);if(!guard.held)return Result::Busy;
        if(state.recovering || !state.fabric.begin_recovery(0))return Result::Rejected;
        state.recovering=true;state.quiet=0;state.dma_quiet=false;return Result::Done;
    }
    Result dma(bool quiet) {
        Guard guard(gate);if(!guard.held)return Result::Busy;
        state.dma_quiet=quiet;return Result::Done;
    }
    Result attest(unsigned mask) {
        Guard guard(gate);if(!guard.held)return Result::Busy;
        if(!state.recovering || !mask || mask>7 || ((mask&4) && !state.dma_quiet))return Result::Rejected;
        if((mask&3) && state.active)return Result::Busy;
        for(unsigned core=0;core<2;++core)
            if(((mask&1) && State::producer(core)) || ((mask&2) && State::consumer(core)))
                if(state.enabled&(1u<<core))return Result::Rejected;
        if(!state.fabric.attest(0,mask))return Result::Rejected;
        state.quiet|=uint8_t(mask);return Result::Done;
    }
    Result restart() {
        Guard guard(gate);if(!guard.held)return Result::Busy;
        if(state.active)return Result::Busy;
        if(!state.recovering || state.quiet!=7)return Result::Rejected;
        if(!state.fabric.restart(0))return Result::Exhausted;
        ++state.epoch;state.hints[0]=state.hints[1]=0;state.recovering=false;return Result::Done;
    }
    // Observer runs under the same Gate; must not call View or run user code.
    template<class Observer> Result observe(Observer& observer) {
        Guard guard(gate);if(!guard.held)return Result::Busy;
        observer.session(state.epoch,state.recovering,state.enabled,state.active,state.quiet,state.dma_quiet,state.hints[0],state.hints[1]);
        state.fabric.observe(observer);return Result::Done;
    }
};
} // namespace h417_transport_experiment
