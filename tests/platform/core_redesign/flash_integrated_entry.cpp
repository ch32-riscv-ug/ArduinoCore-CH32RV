#include "../../unit/core_redesign/flash_integrated.hpp"
using namespace flash_experiment;
extern "C" uint8_t flash_probe_read(uint32_t);
extern "C" bool flash_probe_erase(uint32_t,unsigned);
extern "C" bool flash_probe_program(uint32_t,const uint8_t*,unsigned);
struct Physical {
    static uint8_t read(uint32_t a){return flash_probe_read(a);}
    static bool erase(uint32_t a,unsigned n){return flash_probe_erase(a,n);}
    static bool program(uint32_t a,const uint8_t* p,unsigned n){return flash_probe_program(a,p,n);}
};
struct Geometry {
    static constexpr unsigned max_program_bytes=PROGRAM;
    static constexpr unsigned program(){return PROGRAM;}
    static constexpr unsigned erase(){return ERASE;}
    static constexpr unsigned units(){return UNITS;}
    static constexpr uint32_t origin(){return REGION_ORIGIN;}
};
struct Relative {
    static uint8_t read(uint32_t a){return Physical::read(Geometry::origin()+a);}
    static bool erase(uint32_t a,unsigned n){return Physical::erase(Geometry::origin()+a,n);}
    static bool program(uint32_t a,const uint8_t* p,unsigned n){return Physical::program(Geometry::origin()+a,p,n);}
};
volatile uint32_t operation,argument[3],observed[8];
uint8_t payload[12]{};uint32_t answer=0;
#if STYLE == 2 || STYLE == 3 || STYLE == 4
RegionTable<2> registry_probe;
#endif
#if CASE > 0
constexpr Kind kind=CASE==1?Kind::Raw:((CASE==4||CASE==5||CASE==7)?Kind::Keys:Kind::Blob);
constexpr bool two=CASE==3||CASE==5||CASE==6||CASE==7,commit=CASE==6||CASE==7;
#if STYLE == 1
Store<Relative,Geometry,kind,two,commit> sync_probe;
#else
IntegratedStore<Physical,Geometry,RegionTable<2>,kind,two,commit,32> integrated_probe;
#endif
#endif
#if STYLE == 4
uint32_t owner_tag,table_ticket;
#endif
static void observe(const Snapshot& s){
    observed[1]=unsigned(s.status);observed[2]=s.generation;observed[3]=s.bank;observed[4]=s.size;
    uint32_t hash=0;for(unsigned i=0;i<s.size;++i)hash=(hash<<1)^s.bytes[i];observed[5]=hash;
}
extern "C" __attribute__((noreturn)) void probe_main(){
    for(;;){
#if STYLE == 1
        for(unsigned i=0;i<12;++i)payload[i]=uint8_t(argument[i&1]>>(8*(i>>2)));
        switch(operation){
        case 0:observed[0]=unsigned(sync_probe.begin(payload,argument[0],argument[1]!=0));break;
        case 1:observed[0]=unsigned(sync_probe.step());break;
        case 2:{Snapshot snapshot{};sync_probe.load(snapshot);observe(snapshot);break;}
        case 3:sync_probe.abandon();break;
        case 4:if constexpr(kind==Kind::Keys)observed[0]=unsigned(sync_probe.put(uint16_t(argument[0]),argument[1]));break;
        case 5:if constexpr(kind==Kind::Keys)observed[0]=unsigned(sync_probe.erase_key(uint16_t(argument[0])));break;
        case 6:if constexpr(kind==Kind::Keys)observed[0]=sync_probe.get(uint16_t(argument[0]),answer);observed[6]=answer;break;
        default:observed[0]=unsigned(sync_probe.phase());break;
        }
#elif STYLE == 2
        for(unsigned i=0;i<12;++i)payload[i]=uint8_t(argument[i&1]>>(8*(i>>2)));
        switch(operation){
        case 0:observed[0]=registry_probe.configure(FLASH_ORIGIN,FLASH_BYTES);break;
        case 1:observed[0]=unsigned(integrated_probe.open(registry_probe));break;
        case 2:observed[0]=unsigned(integrated_probe.poll(argument[0]));break;
        case 3:{Snapshot snapshot{};observed[0]=integrated_probe.peek(snapshot);observe(snapshot);break;}
        case 4:observed[0]=integrated_probe.end();break;
        case 5:observed[0]=integrated_probe.rescan();break;
        case 6:observed[0]=unsigned(integrated_probe.begin_update(payload,argument[0],argument[1]!=0));break;
        case 7:observed[0]=unsigned(integrated_probe.write_step());break;
        case 8:if constexpr(kind==Kind::Keys)observed[0]=unsigned(integrated_probe.put(uint16_t(argument[0]),argument[1]));break;
        case 9:if constexpr(kind==Kind::Keys)observed[0]=unsigned(integrated_probe.erase_key(uint16_t(argument[0])));break;
        case 10:if constexpr(kind==Kind::Keys)observed[0]=integrated_probe.get(uint16_t(argument[0]),answer);observed[6]=answer;break;
        default:observed[0]=unsigned(integrated_probe.phase());observed[1]=registry_probe.occupied();observed[2]=registry_probe.last_ticket();break;
        }
#elif STYLE == 4
        switch(operation){
        case 0:observed[0]=registry_probe.configure(FLASH_ORIGIN,FLASH_BYTES);break;
        case 1:observed[0]=unsigned(registry_probe.acquire(&owner_tag,argument[0],argument[1],ERASE,table_ticket));break;
        case 2:observed[0]=registry_probe.release(&owner_tag,argument[0]);break;
        default:observed[0]=registry_probe.owns(&owner_tag,argument[0]);observed[1]=registry_probe.occupied();observed[2]=registry_probe.last_ticket();observed[3]=table_ticket;break;
        }
#else
        asm volatile("");
#endif
    }
}
