#include "../../unit/core_redesign/logical_time.hpp"
using namespace time_experiment;
volatile uint32_t operation,argument[10];
volatile uint64_t observed[10];
Stamp output{};Dropped drop{};Notice notice;
Result last_result=Result::Closed;
#if CASE > 0
constexpr bool wide=CASE==3||CASE==6,assumed=CASE==2||CASE==5,fixed=CASE>=4;
using Conversion=typename std::conditional<fixed,SixMHz,VariableRate>::type;
LogicalClock<wide?64:32,assumed,Conversion> clock_probe;
#if PAIR > 0
DomainTimer<PAIR==2,PAIR==2> timer_probe;
#endif
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main(){
    for(;;){
#if CASE > 0 && !UNUSED
        const Sample sample{uint64_t(argument[0])|(wide?(uint64_t(argument[1])<<32):0),argument[2],argument[3]};
        const uint64_t value=uint64_t(argument[4])|(uint64_t(argument[5])<<32);
        switch(operation){
        case 0:last_result=clock_probe.begin(sample,value,output);break;
        case 1:last_result=clock_probe.observe(sample,Gap{value,argument[6]!=0},output);break;
        case 2:observed[9]=clock_probe.pause();break;
        case 3:observed[9]=clock_probe.stop();break;
        case 4:last_result=clock_probe.resume_known(sample,value,argument[6]!=0,output,drop);break;
        case 5:last_result=clock_probe.resume_unknown(sample,value,output);break;
#if PAIR > 0
        case 7:observed[9]=timer_probe.arm(last_result,output,argument[7],argument[8]);break;
        case 8:observed[9]=unsigned(timer_probe.poll(last_result,output));break;
        case 9:observed[9]=timer_probe.take(notice);break;
#endif
        default:observed[9]=unsigned(last_result);break;
        }
        observed[0]=output.us;observed[1]=(uint64_t(output.epoch)<<32)|output.quality;
        observed[2]=(uint64_t(drop.numerator)<<32)|drop.denominator;
        observed[3]=clock_probe.history_us();observed[4]=clock_probe.history_raw();
        observed[5]=(uint64_t(clock_probe.epoch())<<32)|clock_probe.source();
        observed[6]=(uint64_t(clock_probe.remainder())<<32)|clock_probe.remainder_denominator();
        observed[7]=clock_probe.frequency()|(uint64_t(clock_probe.history_quality())<<32)|(uint64_t(unsigned(clock_probe.phase()))<<40);
#if PAIR > 0
        observed[8]=(uint64_t(notice.expiration.latest_due)<<32)|notice.expiration.skipped;
        observed[9]=(uint64_t(notice.epoch)<<32)|(uint64_t(notice.expiration.saturated)<<16)|timer_probe.pending()|(uint64_t(timer_probe.armed())<<8);
#endif
#else
        asm volatile("");
#endif
    }
}
