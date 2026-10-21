#include "../../unit/core_redesign/completion_arbiter.hpp"
using namespace completion_arbiter_experiment;
constexpr unsigned Records=CASE==2?2:3;
using Queue=Arbiter<3,Records,CASE!=2>;
#if CASE > 0
Queue arbiter_probe;
Queue::Buffer buffers_probe[3];
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation,argument[8],observed[8];
Queue::Token output{};
Queue::Completion completion{};
uint8_t byte=0;
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main() {
    for(;;) {
#if CASE > 0 && !UNUSED
        Queue::Token incoming{argument[0],uint8_t(argument[1]),uint8_t(argument[2])};
        switch(operation) {
        case 0:observed[0]=arbiter_probe.open(argument[0]);break;
        case 1:observed[0]=arbiter_probe.end(argument[0]);break;
        case 2:observed[0]=unsigned(arbiter_probe.submit(argument[0],&buffers_probe[argument[1]%3],argument[2],experiment::IoMode(argument[3]),argument[4],output));break;
        case 3:observed[0]=arbiter_probe.dispatch(output);break;
        case 4:observed[0]=arbiter_probe.finish(incoming,experiment::IoResult(argument[3]),argument[4],uint8_t(argument[5]),argument[6]!=0);break;
        case 5:observed[0]=unsigned(arbiter_probe.cancel(incoming));break;
        case 6:observed[0]=arbiter_probe.notice(incoming);break;
        case 7:observed[0]=arbiter_probe.ack(incoming);break;
        case 8:observed[0]=arbiter_probe.take(incoming,completion);break;
        case 9:observed[0]=buffers_probe[argument[0]%3].write(argument[1],uint8_t(argument[2]));break;
        case 10:observed[0]=buffers_probe[argument[0]%3].read(argument[1],byte);break;
        case 11:{unsigned i=argument[0]%Records;observed[0]=arbiter_probe.phase(i);observed[1]=arbiter_probe.generation(i);break;}
        }
        observed[2]=output.generation;observed[3]=(unsigned(output.index)<<8)|output.client;
        observed[4]=unsigned(completion.result);observed[5]=completion.transferred;observed[6]=completion.digest;
        observed[7]=(arbiter_probe.current()<<24)|(arbiter_probe.cursor()<<16)|byte;
#else
        asm volatile("");
#endif
    }
}
