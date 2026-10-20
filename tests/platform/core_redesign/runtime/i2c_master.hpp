#pragma once
#include <stdint.h>
#include <limits>
#include <type_traits>
namespace i2c_experiment {
constexpr uintptr_t Base=0x40005400u;
enum class Start { Accepted, Busy, Invalid, Unsupported, Closed, Exhausted };
enum class Open { Ready, Busy, Invalid, NotOwned };
enum class Cancel { Requested, AlreadyRequested, AlreadyTerminal, Stale };
enum class Result : uint8_t { None, Success, Failure, Cancelled };
enum class Phase : uint8_t { Idle, Start, Address, TxLoad, TxAck, RxOne, RxMany, RxTwo, RxTail, Stop, Lost, Terminal };
constexpr unsigned C1=0,C2=4,Own1=8,Own2=12,Data=16,S1=20,S2=24,Timing=28;
constexpr uint16_t PE=1,START=0x100,STOP=0x200,ACK=0x400,POS=0x800;
constexpr uint16_t SB=1,ADDR=2,BTF=4,RXNE=0x40,TXE=0x80,ARLO=0x200,Errors=0x1f00;
template<class Bus,class Storage,class Generation> class Master;
template<bool Guarded> struct Loan {
    const void* holder=nullptr;
    bool available() const{return !holder;}
    bool acquire(const void* p){if(holder)return false;holder=p;return true;}
    void release(){holder=nullptr;}
};
template<> struct Loan<false> {
    bool available() const{return true;}
    bool acquire(const void*){return true;}
    void release(){}
};
template<unsigned Capacity,bool Guarded> class Buffer : private Loan<Guarded> {
    static_assert(Capacity>0 && Capacity<=65535,"bounded storage");
    template<class,class,class> friend class Master;
    uint8_t bytes[Capacity]{};
public:
    static constexpr unsigned capacity=Capacity;
    Buffer()=default;Buffer(const Buffer&)=delete;Buffer& operator=(const Buffer&)=delete;
    bool available() const{return Loan<Guarded>::available();}
    bool write(unsigned index,uint8_t v){if(!available()||index>=Capacity)return false;bytes[index]=v;return true;}
    bool read(unsigned index,uint8_t& v) const{if(!available()||index>=Capacity)return false;v=bytes[index];return true;}
};
struct Completion {
    Result result=Result::None;
    uint16_t issued=0,confirmed=0,received=0,flags=0;
};
// One stable record, no DMA/IRQ, exclusive peripheral ownership. critical()
// protects the short ADDR/tail sequences only under the Bus policy. The target
// probe uses IRQ-off preconditions; this is not a validated interrupt backend.
template<class Bus,class Storage,class Generation=uint32_t> class Master {
    static_assert(std::is_unsigned<Generation>::value && !std::is_same<Generation,bool>::value,"unsigned generation");
public:
    using Token=Generation;
private:
    Storage* buffer=nullptr;
    Generation generation=0;
    uint16_t tx_length=0,rx_length=0;
    uint8_t address=0;
    bool is_open=false,cancelled=false,pending=false,receiving=false;
    Phase state=Phase::Idle;
    Completion completion;
    struct Critical { unsigned saved;Critical():saved(Bus::critical_enter()){}~Critical(){Bus::critical_leave(saved);} };
    static uint16_t read(unsigned r){return Bus::read16(Base+r);}
    static void write(unsigned r,uint16_t v){Bus::write16(Base+r,v);}
    static void control(uint16_t clear,uint16_t set){write(C1,uint16_t((read(C1)&~clear)|set));}
    bool matches(Token t) const{return buffer && t && t==generation;}
    void stop_request(Result intended){
        completion.result=intended;state=Phase::Stop;
        Critical guard;
        control(START|ACK|POS,0);
        read(S1); // Re-establish the ADDR clear sequence, after ACK/POS change.
        if(read(S2)&1)control(0,STOP);
    }
    void terminal(){
        control(ACK|POS,0);
        if(!is_open)control(PE,0);
        state=Phase::Terminal;pending=is_open;
    }
    void receive_one(){buffer->bytes[tx_length+completion.received]=uint8_t(read(Data));++completion.received;}
public:
    Master()=default;Master(const Master&)=delete;Master& operator=(const Master&)=delete;
    bool opened() const{return is_open;}
    bool active() const{return buffer!=nullptr;}
    bool terminal_ready() const{return state==Phase::Terminal;}
    bool cancel_pending() const{return cancelled;}
    bool notice_pending() const{return pending;}
    Token token() const{return generation;}
    Phase phase() const{return state;}
    Completion observed() const{return completion;}
    Open open(bool owned,uint32_t pclk,uint32_t requested){
        if(is_open||buffer)return Open::Busy;
        if(!owned)return Open::NotOwned;
        if(pclk<2000000||pclk>48000000||pclk%1000000 ||
           (requested!=100000&&requested!=400000) || (requested==400000&&pclk<4000000))return Open::Invalid;
        const uint32_t divisor=requested*(requested==100000?2u:3u);
        const uint32_t ccr=(pclk+divisor-1)/divisor;
        if(ccr<(requested==100000?4u:1u)||ccr>4095)return Open::Invalid;
        if((read(C1)&(PE|START|STOP|0x8000))||(read(S2)&3))return Open::Busy;
        write(C1,0);write(C2,uint16_t(pclk/1000000)); // Clears IRQ and DMA enables.
        write(Own1,0x4000);write(Own2,0);
        write(Timing,uint16_t(ccr|(requested==400000?0x8000:0)));write(C1,PE);
        is_open=true;return Open::Ready;
    }
    Start start(Storage* data,unsigned tx,unsigned rx,unsigned target){
        if(!is_open)return Start::Closed;
        if(buffer)return Start::Busy;
        if(!data||(!tx&&!rx)||tx>Storage::capacity||rx>Storage::capacity-tx||target>127)return Start::Invalid;
        if(target<8||target>=0x78)return Start::Unsupported;
        if(!data->available())return Start::Busy;
        if(generation==std::numeric_limits<Generation>::max())return Start::Exhausted;
        if((read(C1)&(START|STOP))||(read(S2)&3))return Start::Busy;
        const auto stale=read(S1);
        if(stale&(SB|ADDR|BTF|RXNE))return Start::Busy;
        if(stale&Errors)write(S1,uint16_t(~(stale&Errors)));
        if(!data->acquire(this))return Start::Busy;
        buffer=data;++generation;tx_length=uint16_t(tx);rx_length=uint16_t(rx);address=uint8_t(target);
        completion={};cancelled=pending=false;receiving=tx==0;state=Phase::Start;
        control(ACK|POS,START);return Start::Accepted;
    }
    Cancel cancel(Token t){
        if(!matches(t))return Cancel::Stale;
        if(state==Phase::Terminal)return Cancel::AlreadyTerminal;
        if(cancelled)return Cancel::AlreadyRequested;
        cancelled=true;return Cancel::Requested;
    }
    bool end(){
        if(!is_open)return false;
        is_open=false;pending=false;
        if(buffer&&state!=Phase::Terminal)cancelled=true;
        else control(PE,0);
        return true;
    }
    void step(){
        if(!buffer||state==Phase::Terminal)return;
        const uint16_t flags=read(S1);
        if(flags&Errors){
            completion.flags|=flags&Errors;write(S1,uint16_t(~(flags&Errors)));
            completion.result=Result::Failure;
            if((flags&ARLO)||state==Phase::Lost)state=Phase::Lost;
            else stop_request(Result::Failure);
        }
        if(state==Phase::Stop||state==Phase::Lost){
            const auto ctl=read(C1);const auto status=read(S2);
            if(status&1){if(state==Phase::Stop && !(ctl&STOP))control(START|ACK|POS,STOP);return;}
            if(ctl&(START|STOP))return;
            // No DMA/IRQ can access memory. Drain at most the two receive
            // locations before retiring the local controller transaction.
            for(unsigned i=0;i<2;++i){if(!(read(S1)&RXNE))break;read(Data);}
            if(read(S1)&RXNE)return;
            terminal();return;
        }
        if(cancelled&&(state==Phase::Start||state==Phase::Address||state==Phase::TxLoad)){
            stop_request(Result::Cancelled);return;
        }
        if(state==Phase::Start){
            if(flags&SB){write(Data,uint16_t((address<<1)|(receiving?1:0)));state=Phase::Address;}
        }else if(state==Phase::Address){
            if(!(flags&ADDR))return;
            Critical guard;
            if(!receiving){control(ACK|POS,0);read(S1);read(S2);state=Phase::TxLoad;}
            else if(rx_length==1){control(ACK|POS,0);read(S1);read(S2);control(0,STOP);state=Phase::RxOne;}
            else if(rx_length==2){control(POS|ACK,POS|ACK);read(S1);read(S2);control(ACK,0);state=Phase::RxTwo;}
            else{control(POS,ACK);read(S1);read(S2);state=Phase::RxMany;}
        }else if(state==Phase::TxLoad){
            if(flags&TXE){write(Data,buffer->bytes[completion.issued]);++completion.issued;state=Phase::TxAck;}
        }else if(state==Phase::TxAck){
            if(!(flags&BTF))return;
            ++completion.confirmed;
            if(cancelled&&(completion.confirmed<tx_length||rx_length))stop_request(Result::Cancelled);
            else if(completion.confirmed<tx_length)state=Phase::TxLoad;
            else if(rx_length){receiving=true;control(ACK|POS,START);state=Phase::Start;}
            else stop_request(Result::Success);
        }else if(state==Phase::RxOne){
            if(flags&RXNE){receive_one();completion.result=Result::Success;state=Phase::Stop;}
        }else if(state==Phase::RxMany){
            const unsigned remaining=rx_length-completion.received;
            if(remaining>3){if(flags&RXNE)receive_one();}
            else if(flags&BTF){Critical guard;control(ACK,0);receive_one();state=Phase::RxTail;}
        }else if(state==Phase::RxTwo||state==Phase::RxTail){
            if(flags&BTF){Critical guard;control(0,STOP);receive_one();receive_one();completion.result=Result::Success;state=Phase::Stop;}
        }
    }
    bool peek(Token& t) const{if(!pending||!is_open||state!=Phase::Terminal)return false;t=generation;return true;}
    bool ack(Token t){if(!matches(t)||!pending||!is_open||state!=Phase::Terminal)return false;pending=false;return true;}
    bool take(Token t,Completion& out){
        if(!matches(t)||state!=Phase::Terminal)return false;
        out=completion;pending=false;buffer->release();buffer=nullptr;state=Phase::Idle;return true;
    }
};
} // namespace i2c_experiment
