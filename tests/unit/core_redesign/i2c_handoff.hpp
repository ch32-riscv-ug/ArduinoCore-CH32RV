#pragma once
#include "../../platform/core_redesign/runtime/i2c_master.hpp"

namespace i2c_handoff_experiment {
template<unsigned> class BackendView;
template<class,unsigned,unsigned,bool,unsigned,class,class> class Manager;
template<unsigned Capacity> class Buffer {
    static_assert(Capacity>0&&Capacity<=65535,"bounded bytes");
    template<unsigned> friend class BackendView;
    uint8_t bytes_[Capacity]{};
    const void* owner_=nullptr;
public:
    Buffer()=default;Buffer(const Buffer&)=delete;Buffer& operator=(const Buffer&)=delete;
    bool available() const{return !owner_;}
    bool write(unsigned i,uint8_t v){if(owner_||i>=Capacity)return false;bytes_[i]=v;return true;}
    bool read(unsigned i,uint8_t& v) const{if(owner_||i>=Capacity)return false;v=bytes_[i];return true;}
};

// Private storage view for the unchanged Master. Its release() relinquishes
// only backend access; returning the outer loan requires Manager::take.
template<unsigned Capacity> class BackendView {
    template<class,class,class> friend class i2c_experiment::Master;
    template<class,unsigned,unsigned,bool,unsigned,class,class> friend class Manager;
    struct Bytes {
        Buffer<Capacity>* buffer=nullptr;
        uint8_t& operator[](unsigned i){return buffer->bytes_[i];}
    } bytes;
    const void* backend_=nullptr;
    bool reserve(Buffer<Capacity>& buffer){
        if(bytes.buffer||!buffer.available())return false;
        bytes.buffer=&buffer;buffer.owner_=this;return true;
    }
    bool acquire(const void* backend){if(!available())return false;backend_=backend;return true;}
    void release(){backend_=nullptr;}
    bool return_loan(){
        if(!bytes.buffer||backend_||bytes.buffer->owner_!=this)return false;
        bytes.buffer->owner_=nullptr;bytes.buffer=nullptr;return true;
    }
public:
    static constexpr unsigned capacity=Capacity;
    BackendView()=default;BackendView(const BackendView&)=delete;BackendView& operator=(const BackendView&)=delete;
    bool available() const{return bytes.buffer&&bytes.buffer->owner_==this&&!backend_;}
};

enum class Phase:uint8_t { Free,Queued,Running,Terminal };
enum class Dispatch:uint8_t { Started,Busy,Idle,Closed,Exhausted };
// Serial normal context. The caller supplies exclusive pins/clock/peripheral
// ownership and the existing Master's IRQ-off register-access preconditions.
template<class Bus,unsigned Clients,unsigned Records,bool Dedicated,unsigned Capacity=32,class Generation=uint32_t,class BackendGeneration=uint32_t>
class Manager {
    static_assert(Clients>0&&Clients<=255&&Records>0&&Records<=255,"bounded records");
    static_assert(!Dedicated||Records==Clients,"dedicated record per client");
    static_assert(std::is_unsigned<Generation>::value&&!std::is_same<Generation,bool>::value,"unsigned request generation");
    using View=BackendView<Capacity>;
    i2c_experiment::Master<Bus,View,BackendGeneration> backend_;
    struct Record {
        View view;
        i2c_experiment::Completion completion{};
        Generation generation=0;
        uint16_t tx=0,rx=0;
        uint8_t client=0,address=0;
        Phase phase=Phase::Free;
        bool cancelled=false,pending=false;
    } records_[Records];
    bool opened_[Clients]{};
    uint8_t cursor_=0,current_=0;
    Record* client(unsigned c){for(auto& r:records_)if(r.phase!=Phase::Free&&r.client==c)return &r;return nullptr;}
public:
    struct Token {Generation generation;uint8_t index,client;};
    using Completion=i2c_experiment::Completion;
    using Storage=Buffer<Capacity>;
private:
    Record* find(Token t){
        if(!t.generation||t.index>=Records||t.client>=Clients)return nullptr;
        auto& r=records_[t.index];
        return r.phase!=Phase::Free&&r.generation==t.generation&&r.client==t.client?&r:nullptr;
    }
    void cancel_queued(Record& r){
        r.cancelled=true;r.completion={};r.completion.result=i2c_experiment::Result::Cancelled;
        r.phase=Phase::Terminal;r.pending=opened_[r.client];
    }
public:
    Manager()=default;Manager(const Manager&)=delete;Manager& operator=(const Manager&)=delete;
    i2c_experiment::Open open_backend(bool owned,uint32_t pclk,uint32_t rate){return backend_.open(owned,pclk,rate);}
    bool close_backend(){
        for(const auto& r:records_)if(r.phase==Phase::Queued||r.phase==Phase::Running)return false;
        return backend_.end();
    }
    bool open(unsigned c){if(c>=Clients||opened_[c]||client(c))return false;opened_[c]=true;return true;}
    bool end(unsigned c){
        if(c>=Clients||!opened_[c])return false;
        opened_[c]=false;
        if(auto* r=client(c)){
            r->pending=false;
            if(r->phase==Phase::Queued)cancel_queued(*r);
            else if(r->phase==Phase::Running){r->cancelled=true;backend_.cancel(backend_.token());}
        }
        return true;
    }
    i2c_experiment::Start submit(unsigned c,Storage* buffer,unsigned tx,unsigned rx,unsigned address,Token& out){
        using i2c_experiment::Start;
        if(c>=Clients)return Start::Invalid;
        if(!opened_[c]||!backend_.opened())return Start::Closed;
        if(client(c))return Start::Busy;
        if(!buffer||(!tx&&!rx)||tx>Capacity||rx>Capacity-tx||address>127)return Start::Invalid;
        if(address<8||address>=0x78)return Start::Unsupported;
        if(!buffer->available())return Start::Busy;
        unsigned index=Records;bool live=false;
        for(unsigned i=0;i<Records;++i){
            if(Dedicated&&i!=c)continue;
            auto& r=records_[i];if(r.generation==std::numeric_limits<Generation>::max())continue;
            live=true;if(r.phase==Phase::Free){index=i;break;}
        }
        if(index==Records)return live?Start::Busy:Start::Exhausted;
        auto& r=records_[index];if(!r.view.reserve(*buffer))return Start::Busy;
        ++r.generation;r.tx=uint16_t(tx);r.rx=uint16_t(rx);r.address=uint8_t(address);r.client=uint8_t(c);
        r.completion={};r.cancelled=r.pending=false;r.phase=Phase::Queued;
        out={r.generation,uint8_t(index),uint8_t(c)};return Start::Accepted;
    }
    Dispatch dispatch(Token& out){
        if(current_)return Dispatch::Busy;
        if(!backend_.opened())return Dispatch::Closed;
        for(unsigned offset=0;offset<Clients;++offset){
            const unsigned c=(cursor_+offset)%Clients;auto* r=client(c);
            if(!r||r->phase!=Phase::Queued)continue;
            const auto started=backend_.start(&r->view,r->tx,r->rx,r->address);
            if(started!=i2c_experiment::Start::Accepted)return started==i2c_experiment::Start::Exhausted?Dispatch::Exhausted:Dispatch::Busy;
            const unsigned i=unsigned(r-records_);r->phase=Phase::Running;current_=uint8_t(i+1);cursor_=uint8_t((c+1)%Clients);
            out={r->generation,uint8_t(i),uint8_t(c)};return Dispatch::Started;
        }
        return Dispatch::Idle;
    }
    void step(){if(current_)backend_.step();}
    bool reap(){
        if(!current_||!backend_.terminal_ready())return false;
        auto& r=records_[current_-1];
        if(!backend_.take(backend_.token(),r.completion))return false;
        r.phase=Phase::Terminal;r.pending=opened_[r.client];current_=0;return true;
    }
    i2c_experiment::Cancel cancel(Token t){
        auto* r=find(t);if(!r)return i2c_experiment::Cancel::Stale;
        if(r->phase==Phase::Terminal)return i2c_experiment::Cancel::AlreadyTerminal;
        if(r->cancelled)return i2c_experiment::Cancel::AlreadyRequested;
        r->cancelled=true;
        if(r->phase==Phase::Queued){cancel_queued(*r);return i2c_experiment::Cancel::Requested;}
        return backend_.cancel(backend_.token());
    }
    bool notice(Token t){auto* r=find(t);return r&&r->phase==Phase::Terminal&&r->pending&&opened_[r->client];}
    bool ack(Token t){auto* r=find(t);if(!r||!notice(t))return false;r->pending=false;return true;}
    bool take(Token t,Completion& out){
        auto* r=find(t);if(!r||r->phase!=Phase::Terminal||!r->view.return_loan())return false;
        out=r->completion;r->phase=Phase::Free;r->pending=false;return true;
    }
    unsigned current() const{return current_?unsigned(current_-1):Records;}
    unsigned cursor() const{return cursor_;}
    unsigned phase(unsigned i) const{return unsigned(records_[i].phase);}
    unsigned owner(unsigned i) const{return records_[i].client;}
    unsigned generation(unsigned i) const{return records_[i].generation;}
    bool opened(unsigned c) const{return opened_[c];}
    bool backend_opened() const{return backend_.opened();}
    bool backend_active() const{return backend_.active();}
    bool backend_terminal() const{return backend_.terminal_ready();}
    unsigned backend_phase() const{return unsigned(backend_.phase());}
    unsigned backend_generation() const{return backend_.token();}
};
} // namespace i2c_handoff_experiment
