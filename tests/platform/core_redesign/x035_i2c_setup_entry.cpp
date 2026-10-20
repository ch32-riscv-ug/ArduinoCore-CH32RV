#include "../../unit/core_redesign/x035_i2c_setup.hpp"
using namespace i2c_resource_experiment;
// Structural link probe only; no startup, GPIO setup or IRQ-safe backend.
struct Bus {
    static uint32_t read32(uintptr_t p){return *reinterpret_cast<volatile uint32_t*>(p);}
    static void write32(uintptr_t p,uint32_t v){*reinterpret_cast<volatile uint32_t*>(p)=v;}
    static uint16_t read16(uintptr_t p){return *reinterpret_cast<volatile uint16_t*>(p);}
    static void write16(uintptr_t p,uint16_t v){*reinterpret_cast<volatile uint16_t*>(p)=v;}
    static unsigned critical_enter(){asm volatile("":::"memory");return 0;}
    static void critical_leave(unsigned){asm volatile("":::"memory");}
};
struct Pins {bool prepared=false;bool ready() const{return prepared;}};
using Resources=Domain<2>;
using Storage=i2c_experiment::Buffer<64,true>;
#if CASE == 1
Storage buffer_probe;
i2c_experiment::Master<Bus,Storage> master_probe;
#elif CASE >= 2
Resources domain_probe;
#if CASE == 3 || CASE == 4
Storage buffer_probe;
#if CASE == 4
using Adapter=x035_i2c_setup_experiment::Setup<Bus,Resources>;
Adapter pins_probe(domain_probe);
#else
using Adapter=Pins;Adapter pins_probe;
#endif
Port<Bus,Storage,Adapter,Resources> port_probe(domain_probe,pins_probe);
#endif
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation,argument[5],observed[8];
Resources::Ticket lease{};uint8_t external[2]{};
i2c_experiment::Completion completion{};
uint32_t notice=0;uint8_t byte=0;
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main() {
    for(;;) {
#if CASE > 0 && !UNUSED
#if CASE == 1 || CASE >= 3
#if CASE == 1
        auto& port=master_probe;
#else
        auto& port=port_probe;
#endif
        switch(operation) {
#if CASE == 1
        case 0:observed[0]=unsigned(port.open(true,argument[0],argument[1]));break;
#else
        case 0:observed[0]=unsigned(port.open(argument[1]));break;
#endif
        case 1:observed[0]=unsigned(port.start(&buffer_probe,argument[0],argument[1],argument[2]));break;
        case 2:port.step();break;
        case 3:observed[0]=unsigned(port.cancel(argument[0]));break;
        case 4:observed[0]=port.end();break;
        case 5:observed[0]=port.take(argument[0],completion);break;
        case 6:observed[0]=port.peek(notice);break;
        case 7:observed[0]=port.ack(argument[0]);break;
        case 8:observed[0]=buffer_probe.read(argument[0],byte);break;
        case 9:observed[0]=buffer_probe.write(argument[0],uint8_t(argument[1]));break;
        }
        observed[1]=port.token();observed[2]=unsigned(completion.result);
        observed[3]=(uint32_t(completion.confirmed)<<16)|completion.received;
        observed[4]=notice;observed[5]=byte;
#endif
#if CASE >= 2
        switch(operation) {
        case 20:observed[0]=domain_probe.begin_change();break;
        case 21:observed[0]=domain_probe.publish(argument[0]);break;
        case 22:observed[0]=domain_probe.claim(&external[argument[0]&1],argument[1],lease);break;
        case 23:observed[0]=domain_probe.release(&external[argument[0]&1],{argument[2],uint8_t(argument[3])});break;
#if CASE == 4
        case 24:observed[0]=unsigned(pins_probe.prepare(argument[0]!=0));break;
        case 25:observed[0]=pins_probe.ready();break;
        case 26:observed[0]=pins_probe.abandon(argument[0]!=0);break;
        case 27:observed[0]=x035_i2c_setup_experiment::nominal_pclk<Bus>();break;
#else
#if CASE == 3
        case 24:if(!(domain_probe.held()&6))pins_probe.prepared=argument[0]!=0;break;
#endif
#endif
        }
        observed[6]=domain_probe.frequency();observed[7]=lease.generation;
#endif
#else
        asm volatile("");
#endif
    }
}
