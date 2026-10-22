#pragma once
#include "x0_features.hpp"

namespace experiment {
// N07 capacity change only. Fixed meaning, wire timing and executor policy are
// frozen. Capacity1 rejects new input when full, exactly as capacity4 does.
template<unsigned N> struct CapacityDirectInbox {
    static_assert(N==1 || N==4,"fixed reduction exercise");
    X0Envelope messages[N]{};
    uint8_t head=0,count=0;
    unsigned size() const{return count;}
    bool push(const X0Envelope& value){if(count==N)return false;messages[(head+count)%N]=value;++count;return true;}
    bool pop(X0Envelope& value){if(!count)return false;value=messages[head];head=uint8_t((head+1)%N);--count;return true;}
};
template<unsigned N> struct CapacityBatchInbox {
    using Mailbox=StaticMailbox<X0Envelope,N>;
    Mailbox mailbox;
    const typename Mailbox::Batch* current=nullptr;
    unsigned cursor=0;
    unsigned size() const{return current?current->count-cursor:mailbox.size();}
    bool push(const X0Envelope& value){return mailbox.send(value)==EventSend::Accepted;}
    bool pop(X0Envelope& value){if(!current || cursor==current->count)return false;value=current->messages[cursor++];return true;}
};
template<unsigned Candidate,unsigned N> struct CapacityInbox {using Type=Queue<X0Envelope,N>;};
template<unsigned N> struct CapacityInbox<1,N>{using Type=CapacityDirectInbox<N>;};
template<unsigned N> struct CapacityInbox<4,N>{using Type=CapacityBatchInbox<N>;};
template<unsigned Candidate,unsigned N,bool Touch> struct CapacityX0
    :FeatureAdapter<Candidate,FixedX0Meaning<typename CapacityInbox<Candidate,N>::Type,Touch>> {};
template<unsigned N,bool Touch> struct CapacityX0<4,N,Touch>
    :FixedX0Meaning<CapacityBatchInbox<N>,Touch> {
    void begin(WorkDomain d=WorkDomain::System,bool opt=false){this->pps.inbox.mailbox.begin(d,opt);changed();}
    void changed(){if(this->needs_step())this->pps.inbox.mailbox.notify(1);}
    TurnStatus opportunity(WorkOpportunity where,WorkProfile profile,WorkContext context=WorkContext::Normal){
        using Batch=typename CapacityBatchInbox<N>::Mailbox::Batch;
        return this->pps.inbox.mailbox.deliver(where,profile,context,[&](const Batch& batch){
            this->pps.inbox.current=&batch;this->pps.inbox.cursor=0;
            this->step();this->pps.inbox.current=nullptr;changed();
        });
    }
};
} // namespace experiment
