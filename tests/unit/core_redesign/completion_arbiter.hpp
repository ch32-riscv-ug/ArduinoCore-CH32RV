#pragma once
#include "io_lifecycle.hpp"

namespace completion_arbiter_experiment {
enum class Phase:uint8_t { Free,Queued,Running,Terminal };
// Serial normal-context experiment. IoSlot is a fake peripheral record; start
// reserves the result and loans the buffer, while dispatch grants the bus.
template<unsigned Clients,unsigned Records,bool Dedicated,unsigned Bytes=32,class Generation=uint32_t>
class Arbiter {
    static_assert(Clients>0&&Clients<=255&&Records>0&&Records<=255,"bounded records");
    static_assert(!Dedicated||Records==Clients,"one dedicated record per client");
    using Slot=experiment::IoSlot<Bytes,Generation>;
    struct Record { Slot slot;Phase phase=Phase::Free;uint8_t client=0; } records_[Records];
    bool opened_[Clients]{};
    // Zero means no bus owner; otherwise store record index + 1. This keeps
    // a static instance zero-initialized instead of emitting its whole image.
    uint8_t cursor_=0,current_=0;
public:
    struct Token { Generation generation;uint8_t index,client; };
    using Completion=typename Slot::Completion;
    using Buffer=experiment::IoBuffer<Bytes>;
private:
    Record* find(Token t) {
        if(t.index>=Records||t.client>=Clients||!t.generation)return nullptr;
        auto& r=records_[t.index];
        return r.phase!=Phase::Free&&r.client==t.client&&r.slot.token()==t.generation?&r:nullptr;
    }
    Record* client(unsigned c) {
        for(auto& r:records_)if(r.phase!=Phase::Free&&r.client==c)return &r;
        return nullptr;
    }
    void cancel_queued(Record& r) {
        r.slot.cancel(r.slot.token());
        // This record was never dispatched: the fake producer has no access.
        if(r.slot.hardware_terminal(r.slot.token(),experiment::IoResult::Cancelled,0))r.phase=Phase::Terminal;
    }
public:
    Arbiter()=default;
    Arbiter(const Arbiter&)=delete;Arbiter& operator=(const Arbiter&)=delete;
    bool open(unsigned c) {
        if(c>=Clients||opened_[c]||client(c))return false;
        opened_[c]=true;return true;
    }
    bool end(unsigned c) {
        if(c>=Clients||!opened_[c])return false;
        opened_[c]=false;
        if(auto* r=client(c)) {
            r->slot.end();
            if(r->phase==Phase::Queued)cancel_queued(*r);
        }
        return true;
    }
    experiment::IoStart submit(unsigned c,Buffer* buffer,unsigned length,experiment::IoMode mode,unsigned address,Token& out) {
        using experiment::IoStart;
        if(c>=Clients)return IoStart::Invalid;
        if(!opened_[c])return IoStart::Closed;
        if(client(c))return IoStart::Busy;
        unsigned index=Records;bool live_generation=false;
        for(unsigned i=0;i<Records;++i) {
            if(Dedicated&&i!=c)continue;
            auto& r=records_[i];
            if(r.slot.token()==std::numeric_limits<Generation>::max())continue;
            live_generation=true;
            if(r.phase==Phase::Free){index=i;break;}
        }
        if(index==Records)return live_generation?IoStart::Busy:IoStart::Exhausted;
        auto& r=records_[index];
        if(!r.slot.open())return IoStart::Busy;
        const auto result=r.slot.start(buffer,length,mode,address);
        if(result!=IoStart::Accepted){r.slot.end();return result;}
        r.client=uint8_t(c);r.phase=Phase::Queued;
        out={r.slot.token(),uint8_t(index),uint8_t(c)};return result;
    }
    bool dispatch(Token& out) {
        if(current_)return false;
        for(unsigned offset=0;offset<Clients;++offset) {
            const unsigned c=(cursor_+offset)%Clients;
            auto* r=client(c);
            if(!r||r->phase!=Phase::Queued)continue;
            const unsigned i=unsigned(r-records_);
            r->phase=Phase::Running;current_=uint8_t(i+1);cursor_=uint8_t((c+1)%Clients);
            out={r->slot.token(),uint8_t(i),uint8_t(c)};return true;
        }
        return false;
    }
    bool finish(Token t,experiment::IoResult result,unsigned transferred,uint8_t fill,bool stopped) {
        auto* r=find(t);
        if(!r||r->phase!=Phase::Running||current_!=unsigned(t.index)+1||!stopped)return false;
        if(!r->slot.hardware_terminal(t.generation,result,transferred,fill))return false;
        r->phase=Phase::Terminal;current_=0;return true;
    }
    experiment::IoCancel cancel(Token t) {
        auto* r=find(t);
        if(!r)return experiment::IoCancel::Stale;
        const auto result=r->slot.cancel(t.generation);
        if(r->phase==Phase::Queued)cancel_queued(*r);
        return result;
    }
    bool notice(Token t) {
        auto* r=find(t);Generation generation=0;
        return r&&r->slot.peek_notice(generation)&&generation==t.generation;
    }
    bool ack(Token t){auto* r=find(t);return r&&r->slot.ack_notice(t.generation);}
    bool take(Token t,Completion& out) {
        auto* r=find(t);
        if(!r||r->phase!=Phase::Terminal||!r->slot.take(t.generation,out))return false;
        r->slot.end();r->phase=Phase::Free;return true;
    }
    // Serial observer only; callers must keep client/record indices in range.
    bool opened(unsigned c) const{return opened_[c];}
    unsigned cursor() const{return cursor_;}
    unsigned current() const{return current_?unsigned(current_-1):Records;}
    unsigned phase(unsigned i) const{return unsigned(records_[i].phase);}
    unsigned owner(unsigned i) const{return records_[i].client;}
    unsigned generation(unsigned i) const{return records_[i].slot.token();}
    bool pending(unsigned i) const{return records_[i].slot.notification_pending();}
    bool cancelled(unsigned i) const{return records_[i].slot.cancel_pending();}
};
} // namespace completion_arbiter_experiment
