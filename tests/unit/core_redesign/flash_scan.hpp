#pragma once
#include "flash_store.hpp"
#include <limits>
#include <type_traits>

// Serialized experiment only. All cooperating accesses use this one table and
// canonical physical addresses. No ISR, multicore, alias normalization, or
// protection against code bypassing the table is provided.
namespace flash_experiment {
enum class Claim : uint8_t { Accepted, Busy, Invalid, Exhausted, Unsupported };
template<unsigned Slots,class Generation=uint32_t> class RegionTable {
    static_assert(Slots>0&&Slots<=255,"bounded region table");
    static_assert(std::is_unsigned<Generation>::value&&!std::is_same<Generation,bool>::value,"unsigned ticket");
    struct Entry { const void* owner=nullptr;uint32_t start=0,length=0;Generation ticket=0; };
    Entry entries[Slots]{};
    uint32_t origin=0,length=0;
    Generation sequence=0;
public:
    using Ticket=Generation;
    RegionTable()=default;RegionTable(const RegionTable&)=delete;RegionTable& operator=(const RegionTable&)=delete;
    unsigned occupied() const{unsigned n=0;for(const auto& e:entries)if(e.owner)++n;return n;}
    bool configure(uint32_t base,uint32_t bytes){
        if(occupied()||!bytes||bytes>0xffffffffu-base)return false;
        origin=base;length=bytes;return true; // Does not reset the ticket sequence.
    }
    Claim acquire(const void* owner,uint32_t start,uint32_t bytes,uint32_t alignment,Ticket& out){
        if(!owner||!bytes||!alignment||(alignment&(alignment-1))||
           (start&(alignment-1))||(bytes&(alignment-1))||start<origin||
           start-origin>length||bytes>length-(start-origin))return Claim::Invalid;
        Entry* free=nullptr;
        for(auto& e:entries){
            if(!e.owner){if(!free)free=&e;continue;}
            if(e.owner==owner||(start<e.start+e.length&&e.start<start+bytes))return Claim::Busy;
        }
        if(!free)return Claim::Busy;
        if(sequence==std::numeric_limits<Generation>::max())return Claim::Exhausted;
        ++sequence;free->owner=owner;free->start=start;free->length=bytes;free->ticket=sequence;out=sequence;
        return Claim::Accepted;
    }
    bool owns(const void* owner,Ticket ticket) const{
        if(!owner||!ticket)return false;
        for(const auto& e:entries)if(e.owner==owner&&e.ticket==ticket)return true;
        return false;
    }
    bool release(const void* owner,Ticket ticket){
        if(!owner||!ticket)return false;
        for(auto& e:entries)if(e.owner==owner&&e.ticket==ticket){e.owner=nullptr;return true;}
        return false;
    }
    Ticket last_ticket() const{return sequence;}
};

enum class ScanPhase : uint8_t { Idle, Probe, Header, Payload, Footer, Raw, Done };
template<bool Raw> struct ScanStorage {
    uint8_t header_bytes[16]{};
    Snapshot current{},result{};
    uint32_t crc=0;
    uint16_t cursor=0;
    uint8_t bank=0;
    ScanPhase scan_phase=ScanPhase::Idle;
};
template<> struct ScanStorage<true> {
    Snapshot result{};
    uint16_t cursor=0;
    ScanPhase scan_phase=ScanPhase::Idle;
};

// Geometry adds origin(): canonical physical start of the region. No M call
// occurs outside poll(). Every transition uses buffered bytes, not extra reads.
template<class M,class G,Kind K,bool Two=false,bool Commit=false>
class ByteScanner : private ScanStorage<K==Kind::Raw> {
    static_assert(K!=Kind::Raw||(!Two&&!Commit),"raw has no bank selection metadata");
    static uint32_t rounded(uint32_t n){return ((n+G::program()-1)/G::program())*G::program();}
    static uint32_t footer(){return Commit?rounded(12):0;}
    static uint32_t bank_bytes(){return G::erase()*(G::units()/(Two?2:1));}
    static uint32_t capacity(){return bank_bytes()>16+footer()?bank_bytes()-16-footer():0;}
    static void clear(Snapshot& out,Status status,uint8_t bank=255){
        out.generation=0;out.size=0;out.bank=bank;out.status=status;
        for(unsigned i=0;i<12;++i)out.bytes[i]=0;
    }
    void new_bank(){
        this->cursor=0;this->scan_phase=ScanPhase::Probe;
        clear(this->current,Status::Corrupt,this->bank);
    }
    void finish_bank(Status status){
        if(status==Status::Valid){
            this->current.status=status;
            if(this->result.status==Status::Valid&&this->result.generation==this->current.generation)clear(this->result,Status::Ambiguous);
            else if(this->result.status!=Status::Valid||this->current.generation>this->result.generation)this->result=this->current;
        }else if(status==Status::Corrupt&&this->result.status==Status::Empty)clear(this->result,Status::Corrupt);
        ++this->bank;
        if(this->bank==(Two?2:1))this->scan_phase=ScanPhase::Done;
        else new_bank();
    }
    void finish_body(){
        if constexpr(K==Kind::Keys){
            for(unsigned i=6;i<this->current.size;i+=6){
                if(decode(this->current.bytes+i,2)<=decode(this->current.bytes+i-6,2)){finish_bank(Status::Corrupt);return;}
            }
        }
        if((this->crc^0xffffffffu)!=decode(this->header_bytes+12,4)){finish_bank(Status::Corrupt);return;}
        if constexpr(Commit){this->cursor=0;this->scan_phase=ScanPhase::Footer;}
        else finish_bank(Status::Valid);
    }
    void finish_header(){
        const auto* h=this->header_bytes;
        const char* magic=K==Kind::Keys?"KEY1":"BLB1";
        for(unsigned i=0;i<4;++i)if(h[i]!=uint8_t(magic[i])){finish_bank(Status::Corrupt);return;}
        const auto generation=decode(h+4,4),size=decode(h+8,2),version=decode(h+10,2);
        const bool schema=K==Kind::Keys?(size==0||size==6||size==12):size==8;
        if(!generation||version!=1||!schema||size>capacity()){finish_bank(Status::Corrupt);return;}
        this->current.generation=generation;this->current.size=uint8_t(size);
        this->crc=0xffffffffu;
        for(unsigned i=0;i<12;++i)this->crc=crc_byte(this->crc,h[i]);
        this->cursor=0;this->scan_phase=ScanPhase::Payload;
        if(!size)finish_body();
    }
public:
    ByteScanner()=default;ByteScanner(const ByteScanner&)=delete;ByteScanner& operator=(const ByteScanner&)=delete;
    static uint32_t region_bytes(){return G::erase()*G::units();}
    static bool supported(){
        if(!G::program()||!G::erase()||G::erase()>65535||G::erase()%G::program()||
           !G::units()||G::units()>65535/G::erase()||(Two&&G::units()%2))return false;
        return rounded((K==Kind::Raw?0:16)+(K==Kind::Keys?12:8))+footer()<=bank_bytes();
    }
    void begin(){
        clear(this->result,supported()?Status::Empty:Status::NoSpace);
        this->cursor=0;
        if(!supported()){this->scan_phase=ScanPhase::Done;return;}
        if constexpr(K==Kind::Raw){
            this->result.status=Status::Unvalidated;this->result.size=8;this->result.bank=0;this->scan_phase=ScanPhase::Raw;
        }else{this->bank=0;new_bank();}
    }
    bool done() const{return this->scan_phase==ScanPhase::Done;}
    ScanPhase phase() const{return this->scan_phase;}
    const Snapshot& snapshot() const{return this->result;}
    void reset(){this->scan_phase=ScanPhase::Idle;}
    void poll(unsigned budget){
        while(budget&&this->scan_phase!=ScanPhase::Idle&&!done()){
            --budget;
            if constexpr(K==Kind::Raw){
                this->result.bytes[this->cursor]=M::read(G::origin()+this->cursor);
                if(++this->cursor==8)this->scan_phase=ScanPhase::Done;
            }else{
                const uint32_t base=G::origin()+this->bank*bank_bytes();
                if(this->scan_phase==ScanPhase::Probe){
                    const auto value=M::read(base+this->cursor);++this->cursor;
                    if(value!=255){this->cursor=0;this->scan_phase=ScanPhase::Header;}
                    else if(this->cursor==bank_bytes())finish_bank(Status::Empty);
                }else if(this->scan_phase==ScanPhase::Header){
                    this->header_bytes[this->cursor]=M::read(base+this->cursor);
                    if(++this->cursor==16)finish_header();
                }else if(this->scan_phase==ScanPhase::Payload){
                    const auto value=M::read(base+16+this->cursor);
                    this->current.bytes[this->cursor++]=value;this->crc=crc_byte(this->crc,value);
                    if(this->cursor==this->current.size)finish_body();
                }else{
                    const unsigned i=this->cursor;
                    const uint8_t expected=i<4?uint8_t("END1"[i]):this->header_bytes[i<8?i:i+4];
                    const auto value=M::read(base+bank_bytes()-footer()+i);++this->cursor;
                    if(value!=expected)finish_bank(Status::Corrupt);
                    else if(this->cursor==12)finish_bank(Status::Valid);
                }
            }
        }
    }
};

enum class ReadState : uint8_t { Idle, Running, Done, Lost };
template<class M,class G,class Table,Kind K,bool Two=false,bool Commit=false,unsigned Quantum=32>
class LeasedScanner {
    static_assert(Quantum>0&&Quantum<=65535,"bounded byte quantum");
    ByteScanner<M,G,K,Two,Commit> scanner;
    Table* table=nullptr;
    typename Table::Ticket ticket=0;
    ReadState state=ReadState::Idle;
public:
    LeasedScanner()=default;LeasedScanner(const LeasedScanner&)=delete;LeasedScanner& operator=(const LeasedScanner&)=delete;
    Claim begin(Table& registry){
        if(state!=ReadState::Idle)return Claim::Busy;
        if(!scanner.supported())return Claim::Unsupported;
        typename Table::Ticket next=0;
        const auto accepted=registry.acquire(this,G::origin(),scanner.region_bytes(),G::erase(),next);
        if(accepted!=Claim::Accepted)return accepted;
        table=&registry;ticket=next;scanner.begin();state=ReadState::Running;return Claim::Accepted;
    }
    bool owned() const{return table&&table->owns(this,ticket);}
    ReadState poll(unsigned budget){
        if(state==ReadState::Idle||state==ReadState::Lost)return state;
        if(!owned()){state=ReadState::Lost;return state;}
        if(state==ReadState::Running){scanner.poll(budget<Quantum?budget:Quantum);if(scanner.done())state=ReadState::Done;}
        return state;
    }
    bool peek(Snapshot& out) const{
        if(state!=ReadState::Done||!owned())return false;
        out=scanner.snapshot();return true;
    }
    bool end(){
        const bool released=table&&table->release(this,ticket);
        table=nullptr;ticket=0;state=ReadState::Idle;scanner.reset();return released;
    }
    ReadState phase() const{return state;}
    ScanPhase scan_phase() const{return scanner.phase();}
    typename Table::Ticket lease_ticket() const{return ticket;}
};
} // namespace flash_experiment
