#pragma once
#include "../../platform/core_redesign/runtime/i2c_master.hpp"

namespace i2c_resource_experiment {
// Serial ledger only. Resource bits are logical conflicts, not GPIO numbers.
template<unsigned Slots=2, class Counter=uint32_t> class Domain {
    static_assert(Slots>0 && Slots<=255,"bounded clients");
    static_assert(std::is_unsigned<Counter>::value && !std::is_same<Counter,bool>::value,"unsigned counter");
public:
    struct Ticket { Counter generation; uint8_t index; };
private:
    struct Lease { const void* owner=nullptr; Counter generation=0; uint8_t mask=0; };
    Lease leases_[Slots]{};
    Counter generation_=0,epoch_=0;
    uint32_t frequency_=0;
    uint8_t held_=0,count_=0;
    bool changing_=false;
public:
    Domain()=default;
    Domain(const Domain&)=delete;Domain& operator=(const Domain&)=delete;
    bool begin_change() {
        if(changing_||count_||epoch_==Counter(-1))return false;
        changing_=true;frequency_=0;return true;
    }
    // Adapter declaration after a confirmed transition, not a measured clock.
    bool publish(uint32_t hz) {
        if(!changing_||hz<2000000||hz>48000000||hz%1000000)return false;
        frequency_=hz;++epoch_;changing_=false;return true;
    }
    bool claim(const void* owner,unsigned mask,Ticket& out) {
        if(!owner||mask>7||!frequency_||changing_||(held_&mask)||generation_==Counter(-1))return false;
        for(const auto& lease:leases_)if(lease.owner==owner)return false;
        for(unsigned i=0;i<Slots;++i)if(!leases_[i].owner) {
            leases_[i]={owner,++generation_,uint8_t(mask)};held_|=uint8_t(mask);++count_;
            out={generation_,uint8_t(i)};return true;
        }
        return false;
    }
    bool release(const void* owner,Ticket ticket) {
        if(ticket.index>=Slots||!ticket.generation)return false;
        auto& lease=leases_[ticket.index];
        if(!owner||lease.owner!=owner||lease.generation!=ticket.generation)return false;
        held_&=uint8_t(~lease.mask);lease.owner=nullptr;lease.mask=0;--count_;return true;
    }
    uint32_t frequency() const{return frequency_;}
    unsigned epoch() const{return epoch_;}
    unsigned count() const{return count_;}
    unsigned held() const{return held_;}
    bool changing() const{return changing_;}
};

// Pins.ready() is an adapter declaration only. No pinmux writes are performed.
// Peripheral+SCL+SDA are logical bits 1|2|4. Clock is held even while open/idle
// and terminal-uncollected, because PE and the timing registers still exist.
template<class Bus,class Storage,class Pins,class Resources> class Port {
    Resources& resources_;
    Pins& pins_;
    i2c_experiment::Master<Bus,Storage> master_;
    typename Resources::Ticket lease_{};
    bool held_=false;
    void retire() {
        if(held_&&!master_.opened()&&!master_.active()) {
            const bool released=resources_.release(this,lease_);
            // Under the declared serial ownership rules release cannot fail.
            if(released)held_=false;
        }
    }
public:
    enum class Open { Ready,Busy,ResourceBusy,PinsNotReady,EngineBusy,Invalid };
    constexpr Port(Resources& resources,Pins& pins):resources_(resources),pins_(pins){}
    Port(const Port&)=delete;Port& operator=(const Port&)=delete;
    Open open(uint32_t requested) {
        if(held_||master_.opened()||master_.active())return Open::Busy;
        if(!resources_.claim(this,7,lease_))return Open::ResourceBusy;
        held_=true;
        if(!pins_.ready()) { resources_.release(this,lease_);held_=false;return Open::PinsNotReady; }
        const auto result=master_.open(true,resources_.frequency(),requested);
        if(result==i2c_experiment::Open::Ready)return Open::Ready;
        resources_.release(this,lease_);held_=false;
        return result==i2c_experiment::Open::Invalid?Open::Invalid:Open::EngineBusy;
    }
    i2c_experiment::Start start(Storage* buffer,unsigned tx,unsigned rx,unsigned address) {
        return master_.start(buffer,tx,rx,address);
    }
    i2c_experiment::Cancel cancel(uint32_t token){return master_.cancel(token);}
    bool end(){const bool result=master_.end();retire();return result;}
    void step(){master_.step();retire();}
    bool peek(uint32_t& token) const{return master_.peek(token);}
    bool ack(uint32_t token){return master_.ack(token);}
    bool take(uint32_t token,i2c_experiment::Completion& out) {
        const bool result=master_.take(token,out);retire();return result;
    }
    bool held() const{return held_;}
    bool opened() const{return master_.opened();}
    bool active() const{return master_.active();}
    bool terminal() const{return master_.terminal_ready();}
    unsigned phase() const{return unsigned(master_.phase());}
    uint32_t token() const{return master_.token();}
    i2c_experiment::Completion observed() const{return master_.observed();}
};
} // namespace i2c_resource_experiment
