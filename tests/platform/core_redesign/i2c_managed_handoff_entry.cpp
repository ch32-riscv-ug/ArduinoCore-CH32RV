#include "../../unit/core_redesign/i2c_managed_handoff.hpp"
// Structural link probe only. Caller precondition is IRQ-off, exclusive
// peripheral/clock/pins. These barriers do not implement an IRQ lock.
struct Bus {
    static uint16_t read16(uintptr_t p){return *reinterpret_cast<volatile uint16_t*>(p);}
    static void write16(uintptr_t p,uint16_t v){*reinterpret_cast<volatile uint16_t*>(p)=v;}
    static unsigned critical_enter(){asm volatile("":::"memory");return 0;}
    static void critical_leave(unsigned){asm volatile("":::"memory");}
};
constexpr unsigned Records=CASE==2?2:3;
using Core=i2c_handoff_experiment::Manager<Bus,3,Records,CASE!=2,BUFFER_BYTES>;
using Domain=i2c_resource_experiment::Domain<2>;
struct Pins {bool valid=false;bool ready() const{return valid;}};
#if MANAGED
using Queue=i2c_managed_handoff_experiment::Managed<Core,Domain>;
#if CASE > 0
Domain resources_probe;Pins pins_probe;
#endif
#else
using Queue=Core;
#endif
#if CASE > 0
Queue manager_probe;
Queue::Storage buffers_probe[3];
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation,argument[8],observed[12];
Queue::Token output{};Queue::Completion completion{};uint8_t byte=0;
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main(){
    for(;;){
#if CASE > 0 && !UNUSED
        Queue::Token incoming{argument[0],uint8_t(argument[1]),uint8_t(argument[2])};
        switch(operation){
        case 0:
#if MANAGED
            observed[0]=unsigned(manager_probe.open_backend(resources_probe,pins_probe,argument[2]));
#else
            observed[0]=unsigned(manager_probe.open_backend(argument[0]!=0,argument[1],argument[2]));
#endif
            break;
        case 1:observed[0]=manager_probe.close_backend();break;
        case 2:observed[0]=manager_probe.open(argument[0]);break;
        case 3:observed[0]=manager_probe.end(argument[0]);break;
        case 4:observed[0]=unsigned(manager_probe.submit(argument[0],&buffers_probe[argument[1]%3],argument[2],argument[3],argument[4],output));break;
        case 5:observed[0]=unsigned(manager_probe.dispatch(output));break;
        case 6:manager_probe.step();break;
        case 7:observed[0]=manager_probe.reap();break;
        case 8:observed[0]=unsigned(manager_probe.cancel(incoming));break;
        case 9:observed[0]=manager_probe.notice(incoming);break;
        case 10:observed[0]=manager_probe.ack(incoming);break;
        case 11:observed[0]=manager_probe.take(incoming,completion);break;
        case 12:observed[0]=buffers_probe[argument[0]%3].write(argument[1],uint8_t(argument[2]));break;
        case 13:observed[0]=buffers_probe[argument[0]%3].read(argument[1],byte);break;
        case 14:{unsigned i=argument[0]%Records;observed[0]=manager_probe.phase(i);observed[11]=manager_probe.generation(i);break;}
#if MANAGED
        case 15:observed[0]=resources_probe.begin_change();break;
        case 16:observed[0]=resources_probe.publish(argument[0]);break;
        case 17:pins_probe.valid=argument[0]!=0;break;
        case 18:observed[0]=resources_probe.frequency();observed[11]=(resources_probe.count()<<16)|resources_probe.held();break;
#endif
        }
        observed[1]=manager_probe.backend_phase();observed[2]=manager_probe.backend_generation();
        observed[3]=(manager_probe.current()<<16)|manager_probe.cursor();
        observed[4]=output.generation;observed[5]=(unsigned(output.index)<<8)|output.client;
        observed[6]=unsigned(completion.result);observed[7]=(uint32_t(completion.issued)<<16)|completion.confirmed;
        observed[8]=(uint32_t(completion.received)<<16)|completion.flags;observed[9]=byte;observed[10]=manager_probe.backend_active();
#else
        asm volatile("");
#endif
    }
}
