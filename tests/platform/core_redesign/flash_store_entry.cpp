#include "../../unit/core_redesign/flash_store.hpp"
using namespace flash_experiment;
extern "C" uint8_t flash_probe_read(uint32_t);
extern "C" bool flash_probe_erase(uint32_t,unsigned);
extern "C" bool flash_probe_program(uint32_t,const uint8_t*,unsigned);
struct Backend {
    static uint8_t read(uint32_t offset){return flash_probe_read(offset);}
    static bool erase(uint32_t offset,unsigned size){return flash_probe_erase(offset,size);}
    static bool program(uint32_t offset,const uint8_t* data,unsigned size){return flash_probe_program(offset,data,size);}
};
struct Geometry {
    static constexpr unsigned max_program_bytes=PROGRAM;
    static constexpr unsigned program(){return PROGRAM;}
    static constexpr unsigned erase(){return ERASE;}
    static constexpr unsigned units(){return UNITS;}
};
volatile uint32_t operation,argument[3],observed[8];
uint8_t input[12];
#if CASE > 0
constexpr Kind kind=CASE==1?Kind::Raw:((CASE==4||CASE==5||CASE==7)?Kind::Keys:Kind::Blob);
constexpr bool two=CASE==3||CASE==5||CASE==6||CASE==7,commit=CASE==6||CASE==7;
Store<Backend,Geometry,kind,two,commit> store_probe;
#endif
extern "C" __attribute__((noreturn)) void probe_main(){
    for(;;){
#if CASE > 0 && !UNUSED
        switch(operation){
        case 0:{
            Snapshot result;store_probe.load(result);
            observed[0]=unsigned(result.status);observed[1]=result.generation;observed[2]=result.bank;observed[3]=result.size;
            uint32_t hash=0;for(unsigned i=0;i<result.size;++i)hash=(hash<<1)^result.bytes[i];observed[4]=hash;break;
        }
        case 1:observed[0]=unsigned(store_probe.begin(input,argument[0],argument[1]!=0));break;
        case 2:observed[0]=unsigned(store_probe.step());break;
        case 3:store_probe.abandon();break;
        case 4:if(argument[0]<12)input[argument[0]]=uint8_t(argument[1]);break;
        case 5:
            if constexpr(kind==Kind::Keys)observed[0]=unsigned(store_probe.put(uint16_t(argument[0]),argument[1]));
            break;
        case 6:
            if constexpr(kind==Kind::Keys)observed[0]=unsigned(store_probe.erase_key(uint16_t(argument[0])));
            break;
        case 7:
            if constexpr(kind==Kind::Keys){uint32_t value=0;observed[0]=store_probe.get(uint16_t(argument[0]),value);observed[1]=value;}
            break;
        default:observed[0]=store_probe.active();observed[1]=unsigned(store_probe.phase());observed[2]=store_probe.supported();break;
        }
#else
        asm volatile("");
#endif
    }
}
