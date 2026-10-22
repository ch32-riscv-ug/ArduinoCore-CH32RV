#pragma once
#include "connection_types.hpp"
#include "io_lifecycle.hpp"
namespace connection_experiment {
// Serial fake peer. Logical disconnect is not MCU power loss. Backend stop is
// explicit (or the fixture's declared auto-stop), never implied by detach.
template<class Inbox,class Policy> struct Pps : Policy {
    Inbox inbox;
    experiment::IoBuffer<8> buffer;
    experiment::IoSlot<8> io;
    typename experiment::IoSlot<8>::Completion last;
    uint32_t now=0,due=0,request_id=0;
    uint32_t accepted_count=0,rejected_count=0,started=0,reclaimed=0,discarded=0;
    int32_t desired=5000,selected=0,sent=0,confirmed=0,measured_mv=0,measured_ma=0;
    bool connected=false,active=false,terminal=false,valid=false,selected_pending=false,stale=true,auto_hardware=true;
    void sync(){active=io.active();terminal=io.terminal();}
    Attach attach(){
        if(connected)return Attach::Already;
        const Attach result=Policy::attach();
        if(result==Attach::Accepted){connected=true;valid=false;selected_pending=false;}
        return result;
    }
    void detach(){connected=false;valid=false;selected_pending=false;if(io.opened())io.end();sync();}
    bool enqueue(int16_t kind){
        if(!connected || (kind!=-1 && kind!=0 && kind!=1) || !inbox.push(Policy::pack(kind))){++rejected_count;return false;}
        ++accepted_count;return true;
    }
    bool finish(uint32_t token,uint32_t epoch,experiment::IoResult result,unsigned count,bool quiescent){
        if(!quiescent || epoch!=Policy::request_epoch())return false;
        const bool ok=io.hardware_terminal(token,result,count);sync();return ok;
    }
    bool completion(uint32_t token,uint32_t epoch,bool success){return finish(token,epoch,success?experiment::IoResult::Success:experiment::IoResult::Failure,8,true);}
    void hardware(uint32_t time){
        now=time;
        if(auto_hardware && io.active() && !io.terminal() && now>=due)
            finish(io.token(),Policy::request_epoch(),io.cancel_pending()?experiment::IoResult::Cancelled:experiment::IoResult::Success,8,true);
        sync();
    }
    void measurement(bool ok,int32_t mv,int32_t ma){if(ok){measured_mv=mv;measured_ma=ma;}stale=!ok;}
    void step(){
        if(io.terminal()){
            if(io.take(io.token(),last)){
                ++reclaimed;
                if(last.result==experiment::IoResult::Success && connected && io.opened() && Policy::result_current()){confirmed=sent;valid=true;}
            }
        }
        const unsigned count=inbox.size();
        for(unsigned i=0;i<count;++i){
            typename Policy::Message message{};if(!inbox.pop(message))break;
            if(!connected || !Policy::current(message)){++discarded;continue;}
            const int kind=message.command.kind;
            if(kind==1 && desired<11000)desired+=20;
            if(kind==-1 && desired>5000)desired-=20;
            if(kind==0){selected=desired;selected_pending=true;}
        }
        if(!io.active() && connected){
            if(!io.opened())io.open();
            if(selected_pending){
                for(unsigned i=0;i<4;++i){buffer.write(i,uint8_t(uint32_t(selected)>>(8*i)));buffer.write(4+i,uint8_t(Policy::epoch()>>(8*i)));}
                if(io.start(&buffer,8,experiment::IoMode::Write,0x20)==experiment::IoStart::Accepted){
                    sent=selected;selected_pending=false;Policy::request_started();request_id=io.token();due=now+5;++started;
                }
            }
        }
        sync();
    }
};
}
