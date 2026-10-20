#include "i2c_master.hpp"
using namespace i2c_experiment;

// Link probe only. The independent reset entry leaves maskable interrupts off.
// Calling open/start requires externally prepared clock, pins, and bus ownership.
// No GPIO/clock initialization or physical I2C execution is claimed by this image.
struct I2cMmio {
    static uint16_t read16(uintptr_t p){return *reinterpret_cast<volatile uint16_t*>(p);}
    static void write16(uintptr_t p,uint16_t v){*reinterpret_cast<volatile uint16_t*>(p)=v;}
    static unsigned critical_enter(){asm volatile("":::"memory");return 0;}
    static void critical_leave(unsigned){asm volatile("":::"memory");}
};
volatile uint32_t operation,argument[3],observation[6];
#if CASE != 0
constexpr unsigned capacity=(CASE==2||CASE==5)?32:((CASE==3||CASE==6)?64:8);
constexpr bool guarded=(CASE>=1&&CASE<=3)||CASE==7;
Buffer<capacity,guarded> buffer_probe;
Master<I2cMmio,decltype(buffer_probe)> master_probe;
#endif
extern "C" __attribute__((noreturn)) void experiment_main(){
    for(;;){
#if CASE >= 1 && CASE <= 6
        switch(operation){
        case 0:observation[0]=unsigned(master_probe.open(argument[2]!=0,argument[0],argument[1]));break;
        case 1:observation[0]=unsigned(master_probe.start(&buffer_probe,argument[0],argument[1],argument[2]));break;
        case 2:master_probe.step();break;
        case 3:observation[0]=unsigned(master_probe.cancel(argument[0]));break;
        case 4:observation[0]=master_probe.end();break;
        case 5:{
            Completion result;
            observation[0]=master_probe.take(argument[0],result);
            observation[1]=unsigned(result.result);observation[2]=result.issued;
            observation[3]=result.confirmed;observation[4]=result.received;observation[5]=result.flags;break;
        }
        case 6:{uint32_t token=0;observation[0]=master_probe.peek(token);observation[1]=token;break;}
        case 7:observation[0]=master_probe.ack(argument[0]);break;
        case 8:{uint8_t value=0;observation[0]=buffer_probe.read(argument[0],value);observation[1]=value;break;}
        case 9:observation[0]=buffer_probe.write(argument[0],uint8_t(argument[1]));break;
        default:observation[0]=master_probe.token();observation[1]=unsigned(master_probe.phase());
                observation[2]=master_probe.opened();observation[3]=master_probe.active();
                observation[4]=master_probe.notice_pending();observation[5]=buffer_probe.available();break;
        }
#else
        asm volatile("");
#endif
    }
}
