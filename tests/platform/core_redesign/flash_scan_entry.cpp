#include "../../unit/core_redesign/flash_scan.hpp"
using namespace flash_experiment;
extern "C" uint8_t flash_probe_read(uint32_t);
struct Physical { static uint8_t read(uint32_t address){return flash_probe_read(address);} };
struct Geometry {
    static constexpr unsigned max_program_bytes=PROGRAM;
    static constexpr unsigned program(){return PROGRAM;}
    static constexpr unsigned erase(){return ERASE;}
    static constexpr unsigned units(){return UNITS;}
    static constexpr uint32_t origin(){return REGION_ORIGIN;}
};
struct Relative { static uint8_t read(uint32_t offset){return Physical::read(Geometry::origin()+offset);} };
volatile uint32_t operation,argument[2],observed[8];
#if STYLE == 2 || STYLE == 3 || STYLE == 4
RegionTable<2> registry_probe;
#endif
#if CASE > 0
constexpr Kind kind=CASE==1?Kind::Raw:((CASE==4||CASE==5||CASE==7)?Kind::Keys:Kind::Blob);
constexpr bool two=CASE==3||CASE==5||CASE==6||CASE==7,commit=CASE==6||CASE==7;
#if STYLE == 1
Store<Relative,Geometry,kind,two,commit> sync_probe;
#else
LeasedScanner<Physical,Geometry,RegionTable<2>,kind,two,commit,32> reader_probe;
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
        Snapshot snapshot;sync_probe.load(snapshot);observe(snapshot);observed[0]=operation;
#elif STYLE == 2
        switch(operation){
        case 0:observed[0]=registry_probe.configure(FLASH_ORIGIN,FLASH_BYTES);break;
        case 1:observed[0]=unsigned(reader_probe.begin(registry_probe));break;
        case 2:observed[0]=unsigned(reader_probe.poll(argument[0]));break;
        case 3:{Snapshot snapshot{};observed[0]=reader_probe.peek(snapshot);observe(snapshot);break;}
        case 4:observed[0]=reader_probe.end();break;
        default:observed[0]=reader_probe.owned();observed[1]=unsigned(reader_probe.phase());observed[2]=registry_probe.occupied();observed[3]=registry_probe.last_ticket();break;
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
