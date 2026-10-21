#pragma once
#include <stdint.h>

// EXP-04 candidate, not a public core API or a physical power-loss guarantee.
// M::read is byte-addressed. erase/program return only after the operation is
// quiescent, including failure; neither retains a pointer to scratch storage.
// Exclusive region ownership and serialized calls are caller preconditions.
namespace flash_experiment {
enum class Kind : uint8_t { Raw, Blob, Keys };
enum class Status : uint8_t { Empty, Valid, Corrupt, Ambiguous, Unvalidated, NoSpace };
enum class Start : uint8_t { Accepted, Busy, Closed, Invalid, NoSpace, Corrupt, Ambiguous, Exhausted };
enum class Step : uint8_t { Idle, Pending, Done, Failed, Closed };
enum class Phase : uint8_t { Idle, Erase, Body, Footer, Closed };
struct Snapshot {
    uint32_t generation;
    uint8_t bytes[12];
    uint8_t size,bank;
    Status status;
};
template<bool Formatted> struct Metadata { uint32_t generation_value=0,checksum_value=0; };
template<> struct Metadata<false> {};
inline uint32_t crc_byte(uint32_t crc,uint8_t byte){
    crc^=byte;
    for(unsigned i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
    return crc;
}
inline uint32_t decode(const uint8_t* p,unsigned n){
    uint32_t value=0;
    for(unsigned i=0;i<n;++i)value|=uint32_t(p[i])<<(8*i);
    return value;
}
template<class M,class G,Kind K,bool Two=false,bool Commit=false>
class Store : private Metadata<K!=Kind::Raw> {
    static_assert(K!=Kind::Raw||(!Two&&!Commit),"raw has no selection metadata");
    static_assert(G::max_program_bytes>=2 && G::max_program_bytes<=256,"experiment scratch bound");
    uint8_t payload[K==Kind::Keys?12:8]{};
    uint8_t scratch[G::max_program_bytes]{};
    uint16_t base=0,erase_offset=0,body_offset=0,footer_offset=0;
    uint8_t key_bytes=0;
    Phase state=Phase::Idle;
    template<class,class,class,Kind,bool,bool,unsigned> friend class IntegratedStore;
    // Only the ownership/scan adapter may supply this snapshot. The public
    // synchronous entry points below continue to scan their own media.
    Start begin_prepared(const uint8_t* data,unsigned size,bool recover_corrupt,const Snapshot& current){
        if(state==Phase::Closed)return Start::Closed;
        if(state!=Phase::Idle)return Start::Busy;
        if((!data&&size)||!schema(data,size))return Start::Invalid;
        if(!supported()||size>capacity())return Start::NoSpace;
        if(current.status==Status::Ambiguous)return Start::Ambiguous;
        if(current.status==Status::Corrupt&&!recover_corrupt)return Start::Corrupt;
        if(current.generation==0xffffffffu)return Start::Exhausted;
        for(unsigned i=0;i<size;++i)payload[i]=data[i];
        key_bytes=uint8_t(size);
        if constexpr(K!=Kind::Raw){
            this->generation_value=current.generation+1;
            uint32_t crc=0xffffffffu;
            for(unsigned i=0;i<12;++i)crc=crc_byte(crc,prefix_byte(i));
            for(unsigned i=0;i<size;++i)crc=crc_byte(crc,payload[i]);
            this->checksum_value=crc^0xffffffffu;
        }
        base=uint16_t(Two&&current.status==Status::Valid?(1-current.bank)*bank_bytes():0);
        erase_offset=body_offset=footer_offset=0;state=Phase::Erase;
        return Start::Accepted;
    }
    static uint32_t rounded(uint32_t n){return ((n+G::program()-1)/G::program())*G::program();}
    static uint32_t header(){return K==Kind::Raw?0:16;}
    static uint32_t footer(){return Commit?rounded(12):0;}
    static uint32_t bank_bytes(){return (G::units()/(Two?2:1))*G::erase();}
    static uint32_t capacity(){
        const uint32_t overhead=header()+footer();
        return bank_bytes()>=overhead?bank_bytes()-overhead:0;
    }
    static bool geometry(){
        return G::program()>0 && G::program()<=G::max_program_bytes && G::erase()>0 &&
               G::erase()<=65535 && G::erase()%G::program()==0 && G::units()>0 &&
               G::units()<=65535/G::erase() && (!Two||G::units()%2==0);
    }
    static uint8_t read(uint32_t offset){return M::read(offset);}
    static uint32_t word(uint32_t offset,unsigned n){
        uint32_t value=0;
        for(unsigned i=0;i<n;++i)value|=uint32_t(read(offset+i))<<(8*i);
        return value;
    }
    static void empty(Snapshot& s,Status status,uint8_t bank=255){
        s.generation=0;s.size=0;s.bank=bank;s.status=status;
        for(unsigned i=0;i<12;++i)s.bytes[i]=0;
    }
    static bool schema(const uint8_t* data,unsigned size){
        if constexpr(K!=Kind::Keys)return size==8;
        else{
            if(size!=0&&size!=6&&size!=12)return false;
            for(unsigned i=6;i<size;i+=6)if(decode(data+i,2)<=decode(data+i-6,2))return false;
            return true;
        }
    }
    static uint8_t magic(unsigned index){
        return uint8_t((K==Kind::Keys?"KEY1":"BLB1")[index]);
    }
    static void scan_bank(unsigned index,Snapshot& out){
        empty(out,Status::Empty,uint8_t(index));
        const uint32_t start=index*bank_bytes();
        bool erased=true;
        for(uint32_t i=0;i<bank_bytes();++i)if(read(start+i)!=255){erased=false;break;}
        if(erased)return;
        out.status=Status::Corrupt;
        for(unsigned i=0;i<4;++i)if(read(start+i)!=magic(i))return;
        const auto generation=word(start+4,4),size=word(start+8,2),version=word(start+10,2),checksum=word(start+12,4);
        if(!generation||version!=1||size>capacity()||size>12||(K!=Kind::Keys&&size!=8))return;
        uint8_t data[12];
        for(unsigned i=0;i<size;++i)data[i]=read(start+16+i);
        if(!schema(data,size))return;
        uint32_t crc=0xffffffffu;
        for(unsigned i=0;i<12;++i)crc=crc_byte(crc,read(start+i));
        for(unsigned i=0;i<size;++i)crc=crc_byte(crc,data[i]);
        if((crc^0xffffffffu)!=checksum)return;
        if constexpr(Commit){
            const auto tail=start+bank_bytes()-footer();
            for(unsigned i=0;i<4;++i)if(read(tail+i)!=uint8_t("END1"[i]))return;
            if(word(tail+4,4)!=generation||word(tail+8,4)!=checksum)return;
        }
        out.status=Status::Valid;out.generation=generation;out.size=uint8_t(size);
        for(unsigned i=0;i<size;++i)out.bytes[i]=data[i];
    }
    unsigned payload_size() const{return K==Kind::Keys?key_bytes:8;}
    uint8_t prefix_byte(unsigned i) const{
        if constexpr(K==Kind::Raw)return 0;
        else{
            if(i<4)return magic(i);
            if(i<8)return uint8_t(this->generation_value>>(8*(i-4)));
            if(i<10)return uint8_t(payload_size()>>(8*(i-8)));
            return i==10?1:0;
        }
    }
    uint8_t body_byte(unsigned i) const{
        if constexpr(K==Kind::Raw)return i<8?payload[i]:255;
        else{
            if(i<12)return prefix_byte(i);
            if(i<16)return uint8_t(this->checksum_value>>(8*(i-12)));
            return i<16+payload_size()?payload[i-16]:255;
        }
    }
    uint8_t footer_byte(unsigned i) const{
        if constexpr(K==Kind::Raw)return 255;
        else{
            if(i<4)return uint8_t("END1"[i]);
            if(i<8)return uint8_t(this->generation_value>>(8*(i-4)));
            if(i<12)return uint8_t(this->checksum_value>>(8*(i-8)));
            return 255;
        }
    }
    Start change(uint16_t key,uint32_t value,bool remove){
        static_assert(K==Kind::Keys,"key methods require key candidate");
        if(state==Phase::Closed)return Start::Closed;
        if(state!=Phase::Idle)return Start::Busy;
        Snapshot current;load(current);
        if(current.status==Status::NoSpace)return Start::NoSpace;
        if(current.status==Status::Ambiguous)return Start::Ambiguous;
        if(current.status!=Status::Valid&&current.status!=Status::Empty)return Start::Corrupt;
        uint8_t updated[12];unsigned length=0;bool inserted=false;
        for(unsigned i=0;i<=current.size;i+=6){
            const unsigned previous=i<current.size?decode(current.bytes+i,2):65536;
            if(!inserted&&unsigned(key)<=previous){
                if(!remove){
                    if(length+6>12)return Start::Invalid;
                    updated[length++]=uint8_t(key);updated[length++]=uint8_t(key>>8);
                    for(unsigned j=0;j<4;++j)updated[length++]=uint8_t(value>>(8*j));
                }
                inserted=true;
            }
            if(i<current.size&&previous!=key){
                if(length+6>12)return Start::Invalid;
                for(unsigned j=0;j<6;++j)updated[length++]=current.bytes[i+j];
            }
        }
        return begin(updated,length);
    }
public:
    Store()=default;Store(const Store&)=delete;Store& operator=(const Store&)=delete;
    static bool supported(){
        if(!geometry())return false;
        return rounded(header()+(K==Kind::Keys?12:8))+footer()<=bank_bytes();
    }
    bool active() const{return state!=Phase::Idle&&state!=Phase::Closed;}
    Phase phase() const{return state;}
    void load(Snapshot& out) const{
        empty(out,Status::NoSpace);
        if(!supported())return;
        if constexpr(K==Kind::Raw){
            out.status=Status::Unvalidated;out.size=8;out.bank=0;
            for(unsigned i=0;i<8;++i)out.bytes[i]=read(i);
        }else{
            Snapshot first;scan_bank(0,first);
            if constexpr(Two){
                Snapshot second;scan_bank(1,second);
                if(first.status==Status::Valid&&second.status==Status::Valid){
                    if(first.generation==second.generation){empty(out,Status::Ambiguous);return;}
                    out=first.generation>second.generation?first:second;return;
                }
                if(second.status==Status::Valid){out=second;return;}
                if(first.status==Status::Valid){out=first;return;}
                empty(out,first.status==Status::Empty&&second.status==Status::Empty?Status::Empty:Status::Corrupt);
            }else{
                if(first.status==Status::Valid){out=first;return;}
                empty(out,first.status);
            }
        }
    }
    Start begin(const uint8_t* data,unsigned size,bool recover_corrupt=false){
        if(state==Phase::Closed)return Start::Closed;
        if(state!=Phase::Idle)return Start::Busy;
        if((!data&&size)||!schema(data,size))return Start::Invalid;
        if(!supported()||size>capacity())return Start::NoSpace;
        Snapshot current;load(current);
        if(current.status==Status::Ambiguous)return Start::Ambiguous;
        if(current.status==Status::Corrupt&&!recover_corrupt)return Start::Corrupt;
        if(current.generation==0xffffffffu)return Start::Exhausted;
        for(unsigned i=0;i<size;++i)payload[i]=data[i];
        key_bytes=uint8_t(size);
        if constexpr(K!=Kind::Raw){
            this->generation_value=current.generation+1;
            uint32_t crc=0xffffffffu;
            for(unsigned i=0;i<12;++i)crc=crc_byte(crc,prefix_byte(i));
            for(unsigned i=0;i<size;++i)crc=crc_byte(crc,payload[i]);
            this->checksum_value=crc^0xffffffffu;
        }
        base=uint16_t(Two&&current.status==Status::Valid?(1-current.bank)*bank_bytes():0);
        erase_offset=body_offset=footer_offset=0;state=Phase::Erase;
        return Start::Accepted;
    }
    Step step(){
        if(state==Phase::Closed)return Step::Closed;
        if(state==Phase::Idle)return Step::Idle;
        bool okay=false;
        if(state==Phase::Erase){
            okay=M::erase(base+erase_offset,G::erase());
            if(okay){erase_offset=uint16_t(erase_offset+G::erase());if(erase_offset==bank_bytes())state=Phase::Body;}
        }else if(state==Phase::Body){
            for(unsigned i=0;i<G::program();++i)scratch[i]=body_byte(body_offset+i);
            okay=M::program(base+body_offset,scratch,G::program());
            if(okay){
                body_offset=uint16_t(body_offset+G::program());
                if(body_offset==rounded(header()+payload_size()))state=Commit?Phase::Footer:Phase::Idle;
            }
        }else{
            for(unsigned i=0;i<G::program();++i)scratch[i]=footer_byte(footer_offset+i);
            okay=M::program(base+bank_bytes()-footer()+footer_offset,scratch,G::program());
            if(okay){footer_offset=uint16_t(footer_offset+G::program());if(footer_offset==footer())state=Phase::Idle;}
        }
        if(!okay){state=Phase::Closed;return Step::Failed;}
        return state==Phase::Idle?Step::Done:Step::Pending;
    }
    void abandon(){state=Phase::Closed;}
    Start put(uint16_t key,uint32_t value){return change(key,value,false);}
    Start erase_key(uint16_t key){return change(key,0,true);}
    bool get(uint16_t key,uint32_t& value) const{
        static_assert(K==Kind::Keys,"key methods require key candidate");
        Snapshot current;load(current);
        if(current.status!=Status::Valid)return false;
        for(unsigned i=0;i<current.size;i+=6)if(decode(current.bytes+i,2)==key){value=decode(current.bytes+i+2,4);return true;}
        return false;
    }
};
} // namespace flash_experiment
