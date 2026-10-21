#include "../../unit/core_redesign/save_coexistence.hpp"
#include "../../unit/core_redesign/x0_prepared_wire.hpp"
// Structural probe, same IRQ-off/cold-preparation preconditions as frozen N02.
struct Registers {
    static uint16_t read16(uintptr_t p){return *reinterpret_cast<volatile uint16_t*>(p);}
    static void write16(uintptr_t p,uint16_t v){*reinterpret_cast<volatile uint16_t*>(p)=v;}
    static uint32_t read32(uintptr_t p){return *reinterpret_cast<volatile uint32_t*>(p);}
    static void write32(uintptr_t p,uint32_t v){*reinterpret_cast<volatile uint32_t*>(p)=v;}
    static unsigned critical_enter(){asm volatile("":::"memory");return 0;}
    static void critical_leave(unsigned){asm volatile("":::"memory");}
};
extern "C" uint8_t flash_probe_read(uint32_t);
extern "C" bool flash_probe_erase(uint32_t,unsigned);
extern "C" bool flash_probe_program(uint32_t,const uint8_t*,unsigned);
struct Media {
    static uint8_t read(uint32_t a){return flash_probe_read(a);}
    static bool erase(uint32_t a,unsigned n){return flash_probe_erase(a,n);}
    static bool program(uint32_t a,const uint8_t* p,unsigned n){return flash_probe_program(a,p,n);}
};
struct Geometry {
    static constexpr unsigned max_program_bytes=256;
    static constexpr unsigned program(){return 256;}
    static constexpr unsigned erase(){return 256;}
    static constexpr unsigned units(){return UNITS;}
    static constexpr uint32_t origin(){return 0x08000000u+63488-UNITS*256;}
};
#if CASE > 0
experiment::PreparedX0<FAMILY,Registers,0,x035_i2c_route_experiment::Package::C8,false> application_probe;
experiment::SaveCoexistence<Media,Geometry,UNITS> saver_probe;
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation,argument[4],observed[16];
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main(){
#if CASE > 0 && !UNUSED
    application_probe.bus.initialize();application_probe.begin();saver_probe.open();
#endif
    for(;;){
#if CASE > 0 && !UNUSED
        auto& app=application_probe;
        switch(operation){
        case 0:app.pps.attach();break;
        case 1:observed[0]=app.pps.enqueue(int16_t(argument[0]));break;
        case 2:app.hardware(argument[0]);break;
        case 3:observed[0]=unsigned(app.bus.preparation.prepare(argument[0]!=0));break;
        case 4:app.bus.poll();break;
        case 5:observed[0]=unsigned(saver_probe.request(uint32_t(app.pps.desired),argument[0],argument[1]!=0));break;
        case 6:saver_probe.progress();break;
        case 7:{bool ok=false;observed[0]=saver_probe.take(ok);observed[1]=ok;break;}
        case 8:{flash_experiment::Snapshot s{};observed[0]=saver_probe.peek(s);observed[1]=unsigned(s.status);observed[2]=s.generation;observed[3]=flash_experiment::decode(s.bytes,4);break;}
        case 9:observed[0]=saver_probe.end();break;
        case 10:observed[0]=saver_probe.open();break;
        default:observed[0]=unsigned(app.opportunity(experiment::WorkOpportunity::Explicit,experiment::WorkProfile::M));break;
        }
        app.changed();observed[4]=uint32_t(app.pps.desired);observed[5]=uint32_t(app.pps.confirmed);
        observed[6]=app.pps.accepted_count;observed[7]=app.pps.rejected_count;observed[8]=app.bus.started;observed[9]=app.bus.reclaimed;
        observed[10]=app.measurement_done;observed[11]=app.touch_done;observed[12]=app.screen.displayed_hash;observed[13]=saver_probe.owned();
#if UNITS > 0
        observed[14]=unsigned(saver_probe.driver.phase());observed[15]=saver_probe.result_held;
#else
        observed[14]=99;observed[15]=0;
#endif
#else
        asm volatile("");
#endif
    }
}
