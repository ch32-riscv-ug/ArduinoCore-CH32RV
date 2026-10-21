#pragma once
#include "flash_scan.hpp"

namespace flash_experiment {
enum class UpdateState:uint8_t { Idle,Scanning,Ready,Writing,Done,Failed,Lost };
enum class UpdateStart:uint8_t { Accepted,Busy,Closed,Invalid,NoSpace,Corrupt,Ambiguous,Exhausted,Lost };
// One mount-lifetime lease. The shared scanner reads canonical physical
// addresses; the existing writer's relative offsets are translated below.
// Media erase/program are synchronous and quiescent even on false returns.
template<class M,class G,class Table,Kind K,bool Two=false,bool Commit=false,unsigned Quantum=32>
class IntegratedStore {
    static_assert(Quantum>0&&Quantum<=65535,"bounded read quantum");
    struct Relative {
        static uint8_t read(uint32_t offset){return M::read(G::origin()+offset);}
        static bool erase(uint32_t offset,unsigned n){return M::erase(G::origin()+offset,n);}
        static bool program(uint32_t offset,const uint8_t* p,unsigned n){return M::program(G::origin()+offset,p,n);}
    };
    Store<Relative,G,K,Two,Commit> writer;
    ByteScanner<M,G,K,Two,Commit> scanner;
    Table* table=nullptr;
    typename Table::Ticket ticket=0;
    UpdateState state=UpdateState::Idle;
    UpdateStart ready(){
        if(state==UpdateState::Idle)return UpdateStart::Closed;
        if(!owned()){state=UpdateState::Lost;return UpdateStart::Lost;}
        return state==UpdateState::Ready?UpdateStart::Accepted:UpdateStart::Busy;
    }
    UpdateStart change(uint16_t key,uint32_t value,bool remove){
        static_assert(K==Kind::Keys,"key candidate required");
        auto status=ready();if(status!=UpdateStart::Accepted)return status;
        const auto& current=scanner.snapshot();
        if(current.status==Status::NoSpace)return UpdateStart::NoSpace;
        if(current.status==Status::Ambiguous)return UpdateStart::Ambiguous;
        if(current.status!=Status::Valid&&current.status!=Status::Empty)return UpdateStart::Corrupt;
        uint8_t updated[12];unsigned length=0;bool inserted=false;
        for(unsigned i=0;i<=current.size;i+=6){
            const unsigned previous=i<current.size?decode(current.bytes+i,2):65536;
            if(!inserted&&unsigned(key)<=previous){
                if(!remove){
                    if(length+6>12)return UpdateStart::Invalid;
                    updated[length++]=uint8_t(key);updated[length++]=uint8_t(key>>8);
                    for(unsigned j=0;j<4;++j)updated[length++]=uint8_t(value>>(8*j));
                }
                inserted=true;
            }
            if(i<current.size&&previous!=key){
                if(length+6>12)return UpdateStart::Invalid;
                for(unsigned j=0;j<6;++j)updated[length++]=current.bytes[i+j];
            }
        }
        return begin_update(updated,length);
    }
public:
    IntegratedStore()=default;IntegratedStore(const IntegratedStore&)=delete;IntegratedStore& operator=(const IntegratedStore&)=delete;
    static bool supported(){return Store<Relative,G,K,Two,Commit>::supported()&&ByteScanner<M,G,K,Two,Commit>::supported();}
    bool owned() const{return table&&table->owns(this,ticket);}
    typename Table::Ticket lease_ticket() const{return ticket;}
    UpdateState phase() const{return state;}
    ScanPhase scan_phase() const{return scanner.phase();}
    Phase write_phase() const{return writer.phase();}
    Claim open(Table& registry){
        if(state!=UpdateState::Idle)return Claim::Busy;
        if(!supported())return Claim::Unsupported;
        typename Table::Ticket next=0;
        auto result=registry.acquire(this,G::origin(),scanner.region_bytes(),G::erase(),next);
        if(result!=Claim::Accepted)return result;
        table=&registry;ticket=next;writer.state=Phase::Idle;
        scanner.begin();state=UpdateState::Scanning;return Claim::Accepted;
    }
    UpdateState poll(unsigned budget){
        if(state==UpdateState::Idle||state==UpdateState::Lost)return state;
        if(!owned()){state=UpdateState::Lost;return state;}
        if(state==UpdateState::Scanning){
            scanner.poll(budget<Quantum?budget:Quantum);
            if(scanner.done())state=UpdateState::Ready;
        }
        return state;
    }
    UpdateState write_step(){
        if(state==UpdateState::Idle||state==UpdateState::Lost)return state;
        if(!owned()){state=UpdateState::Lost;return state;}
        if(state==UpdateState::Writing){
            const auto step=writer.step();
            if(step==Step::Done)state=UpdateState::Done;
            else if(step==Step::Failed)state=UpdateState::Failed;
        }
        return state;
    }
    bool peek(Snapshot& out) const{
        if(state!=UpdateState::Ready||!owned())return false;
        out=scanner.snapshot();return true;
    }
    bool get(uint16_t key,uint32_t& out) const{
        static_assert(K==Kind::Keys,"key candidate required");
        if(state!=UpdateState::Ready||!owned()||scanner.snapshot().status!=Status::Valid)return false;
        const auto& s=scanner.snapshot();
        for(unsigned i=0;i<s.size;i+=6)if(decode(s.bytes+i,2)==key){out=decode(s.bytes+i+2,4);return true;}
        return false;
    }
    UpdateStart begin_update(const uint8_t* data,unsigned size,bool recover=false){
        const auto check=ready();if(check!=UpdateStart::Accepted)return check;
        const auto result=writer.begin_prepared(data,size,recover,scanner.snapshot());
        if(result==Start::Accepted){state=UpdateState::Writing;scanner.reset();}
        return UpdateStart(unsigned(result));
    }
    UpdateStart put(uint16_t key,uint32_t value){return change(key,value,false);}
    UpdateStart erase_key(uint16_t key){return change(key,0,true);}
    bool rescan(){
        if(state!=UpdateState::Ready&&state!=UpdateState::Done&&state!=UpdateState::Failed)return false;
        if(!owned()){state=UpdateState::Lost;return false;}
        writer.state=Phase::Idle;scanner.begin();state=UpdateState::Scanning;return true;
    }
    bool end(){
        // No media call is in flight between serialized operations. Stop the
        // volatile plan without rollback, invalidate cache, then release.
        writer.abandon();scanner.reset();
        const bool released=table&&table->release(this,ticket);
        table=nullptr;ticket=0;state=UpdateState::Idle;return released;
    }
};
} // namespace flash_experiment
