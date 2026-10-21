#pragma once
#include "i2c_handoff.hpp"
#include "i2c_resources.hpp"

namespace i2c_managed_handoff_experiment {
// Cooperative, non-revocable resource ownership; serial normal context only.
// This does not configure GPIO/clocks, mask IRQs, or schedule/reap implicitly.
template<class Core,class Resources> class Managed {
    Core core_;
    Resources* resources_=nullptr;
    typename Resources::Ticket lease_{};
    bool release(){
        if(!resources_||!resources_->release(this,lease_))return false;
        resources_=nullptr;lease_={};return true;
    }
public:
    using Token=typename Core::Token;
    using Completion=typename Core::Completion;
    using Storage=typename Core::Storage;
    enum class Open { Ready,Busy,ResourceBusy,PinsNotReady,EngineBusy,Invalid,ResourceFault };
    Managed()=default;Managed(const Managed&)=delete;Managed& operator=(const Managed&)=delete;
    template<class Pins> Open open_backend(Resources& resources,Pins& pins,uint32_t rate){
        if(resources_||core_.backend_opened()||core_.backend_active())return Open::Busy;
        if(!resources.claim(this,7,lease_))return Open::ResourceBusy;
        resources_=&resources;
        if(!pins.ready())return release()?Open::PinsNotReady:Open::ResourceFault;
        const auto result=core_.open_backend(true,resources.frequency(),rate);
        if(result==i2c_experiment::Open::Ready)return Open::Ready;
        if(!release())return Open::ResourceFault;
        return result==i2c_experiment::Open::Invalid?Open::Invalid:Open::EngineBusy;
    }
    bool close_backend(){
        if(!resources_||!core_.close_backend())return false;
        return release();
    }
    bool held() const{return resources_!=nullptr;}
    bool open(unsigned c){return core_.open(c);}
    bool end(unsigned c){return core_.end(c);}
    i2c_experiment::Start submit(unsigned c,Storage* buffer,unsigned tx,unsigned rx,unsigned address,Token& out){
        return core_.submit(c,buffer,tx,rx,address,out);
    }
    i2c_handoff_experiment::Dispatch dispatch(Token& out){return core_.dispatch(out);}
    void step(){core_.step();}
    bool reap(){return core_.reap();}
    i2c_experiment::Cancel cancel(Token t){return core_.cancel(t);}
    bool notice(Token t){return core_.notice(t);}
    bool ack(Token t){return core_.ack(t);}
    bool take(Token t,Completion& out){return core_.take(t,out);}
    unsigned current() const{return core_.current();}
    unsigned cursor() const{return core_.cursor();}
    unsigned phase(unsigned i) const{return core_.phase(i);}
    unsigned owner(unsigned i) const{return core_.owner(i);}
    unsigned generation(unsigned i) const{return core_.generation(i);}
    bool opened(unsigned c) const{return core_.opened(c);}
    bool backend_opened() const{return core_.backend_opened();}
    bool backend_active() const{return core_.backend_active();}
    bool backend_terminal() const{return core_.backend_terminal();}
    unsigned backend_phase() const{return core_.backend_phase();}
    unsigned backend_generation() const{return core_.backend_generation();}
};
} // namespace i2c_managed_handoff_experiment
