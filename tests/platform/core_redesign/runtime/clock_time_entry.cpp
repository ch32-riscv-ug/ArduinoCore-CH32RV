#include "clock_time.hpp"
using namespace runtime_experiment;
struct TimeMmio {
    static uint32_t read(uintptr_t p){return *reinterpret_cast<volatile uint32_t*>(p);}
    static uint32_t read_byte(uintptr_t p){return *reinterpret_cast<volatile uint8_t*>(p);}
    static void write(uintptr_t p,uint32_t v){*reinterpret_cast<volatile uint32_t*>(p)=v;}
};
volatile uint32_t operation,argument[3];
volatile uint64_t observation;
#if CASE == 1 || CASE == 5 || CASE == 6
Clock48<TimeMmio,TARGET_X035!=0> clock_probe;
#endif
#if CASE == 2 || CASE == 3 || CASE == 5 || CASE == 7
Counter<TimeMmio,TARGET_X035!=0> counter_probe;
#endif
extern "C" __attribute__((noreturn)) void experiment_main(){
    for(;;){
#if CASE == 1
        switch(operation%3){
        case 0:observation=unsigned(clock_probe.begin(argument[0]!=0));break;
        case 1:observation=unsigned(clock_probe.step());break;
        default:observation=clock_probe.abandon();break;
        }
#elif CASE == 2 || CASE == 3
        switch(operation%4){
        case 0:observation=unsigned(counter_probe.begin(argument[0],argument[1]!=0,argument[2]!=0));break;
        case 1:observation=counter_probe.stop();break;
        case 2:{
#if CASE == 2
            uint32_t value=0;const auto s=counter_probe.low(argument[0],value);
#else
            uint64_t value=0;const auto s=counter_probe.sample(argument[0],value,argument[1]);
#endif
            observation=value^uint64_t(unsigned(s));break;
        }
        default:observation=counter_probe.frequency()^counter_probe.epoch();break;
        }
#elif CASE == 4
        if(argument[1])observation=elapsed_microseconds(argument[0],argument[1]);
        else observation=0xbad;
#elif CASE == 8
        observation=elapsed_microseconds(argument[0],6000000);
#elif CASE == 9
        observation=argument[0]/6u;
#elif CASE == 5
        switch(operation%6){
        case 0:observation=unsigned(clock_probe.begin(argument[0]!=0));break;
        case 1:observation=unsigned(clock_probe.step());break;
        case 2:observation=clock_probe.stage==ClockStage::Complete?unsigned(counter_probe.begin(48000000,true,argument[0]!=0)):0x100;break;
        case 3:{uint64_t value=0;const auto s=counter_probe.sample(argument[0],value,3);observation=value^uint64_t(unsigned(s));break;}
        case 4:observation=counter_probe.stop();break;
        default:observation=clock_probe.abandon();break;
        }
#else
        asm volatile("");
#endif
    }
}
