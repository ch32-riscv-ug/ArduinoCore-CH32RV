#pragma once
#include "event_executor.hpp"

namespace completion_fanout_experiment {
enum class Reserve { Accepted, Busy, Invalid, Exhausted };
enum class Send { Accepted, Full, Stale, Queued, Delivered, NotReady, Invalid };
// One bounded metadata publication, not shared ownership of an I/O buffer.
// Reserve before taking the source result, then publish its copied value.
// All methods require serial calls; there is no dispatch or implicit retry.
template<class Value,unsigned Targets,unsigned Capacity=1,class Generation=uint32_t>
class Publication {
    static_assert(Targets>0&&Targets<=8,"bounded recipient mask");
    static_assert(std::is_unsigned<Generation>::value&&!std::is_same<Generation,bool>::value,"unsigned generation");
public:
    struct Payload { Generation generation=0;uint8_t recipient=0;Value value{}; };
    using Events=experiment::EventExecutor<Payload,Targets,Capacity>;
    using Target=typename Events::Token;
    using Ticket=Generation;
private:
    Value value_{};
    Target routes_[Targets]{};
    Generation generation_=0;
    uint8_t phase_=0,required_=0,queued_=0,delivered_=0;
    bool matches(Ticket ticket) const {return phase_&&ticket&&ticket==generation_;}
    bool selected(unsigned i) const {return i<Targets&&(required_&(1u<<i));}
public:
    Publication()=default;
    Publication(const Publication&)=delete;Publication& operator=(const Publication&)=delete;
    Reserve reserve(const Target* routes,unsigned mask,Ticket& out) {
        if(phase_)return Reserve::Busy;
        if(!routes||!mask||(mask&~((1u<<Targets)-1)))return Reserve::Invalid;
        if(generation_==std::numeric_limits<Generation>::max())return Reserve::Exhausted;
        ++generation_;phase_=1;required_=uint8_t(mask);queued_=delivered_=0;
        for(unsigned i=0;i<Targets;++i)routes_[i]=routes[i];
        out=generation_;return Reserve::Accepted;
    }
    bool publish(Ticket ticket,const Value& value) {
        if(!matches(ticket)||phase_!=1)return false;
        value_=value;phase_=2;return true;
    }
    bool abandon(Ticket ticket) {
        if(!matches(ticket)||phase_!=1)return false;
        phase_=required_=queued_=delivered_=0;return true;
    }
    Send pump(Events& events,Ticket ticket,unsigned i) {
        if(!matches(ticket)||phase_!=2)return Send::NotReady;
        if(!selected(i))return Send::Invalid;
        const unsigned bit=1u<<i;
        if(delivered_&bit)return Send::Delivered;
        if(queued_&bit)return Send::Queued;
        const auto result=events.send(routes_[i],Payload{ticket,uint8_t(i),value_});
        if(result==experiment::EventSend::Accepted){queued_|=bit;return Send::Accepted;}
        return result==experiment::EventSend::Full?Send::Full:Send::Stale;
    }
    // Retargeting queued hints is allowed only once the old registration is
    // gone. The caller explicitly chooses the replacement recipient binding.
    bool rebind(Events& events,Ticket ticket,unsigned i,Target replacement) {
        if(!matches(ticket)||phase_!=2||!selected(i)||(delivered_&(1u<<i))||
           events.valid(routes_[i])||!events.valid(replacement))return false;
        routes_[i]=replacement;queued_&=~(1u<<i);return true;
    }
    bool receive(const typename Events::Delivery& delivery,Value& out) {
        const auto& p=delivery.message;
        if(!delivery.has_message||!matches(p.generation)||phase_!=2||!selected(p.recipient))return false;
        const unsigned bit=1u<<p.recipient;
        const auto route=routes_[p.recipient];
        if(!(queued_&bit)||(delivered_&bit)||route.index!=delivery.target.index||route.generation!=delivery.target.generation)return false;
        out=p.value;queued_&=~bit;delivered_|=bit;return true;
    }
    bool release(Ticket ticket) {
        if(!matches(ticket)||phase_!=2||delivered_!=required_)return false;
        phase_=required_=queued_=delivered_=0;return true;
    }
    unsigned phase() const{return phase_;}
    unsigned generation() const{return generation_;}
    unsigned required() const{return required_;}
    unsigned queued() const{return queued_;}
    unsigned delivered() const{return delivered_;}
};
} // namespace completion_fanout_experiment
