#include "../../unit/core_redesign/completion_arbiter.hpp"
#include "../../unit/core_redesign/completion_delivery.hpp"
using namespace experiment;
constexpr bool Pooled=CASE==2||CASE==4;
using Bus=completion_arbiter_experiment::Arbiter<CLIENTS,Pooled?2:CLIENTS,!Pooled,BUFFER_BYTES>;
using Events=EventExecutor<Bus::Token,MAILBOX_TARGETS,MAILBOX_CAPACITY>;
#if CASE > 0
Bus arbiter_probe;
Bus::Buffer buffers_probe[CLIENTS];
#if CASE >= 3
Events events_probe;
Events::Token target_probe{};
#endif
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation,argument[8],observed[12],counts[3];
Bus::Token output{};
Bus::Completion completion{};
uint8_t byte=0;
static void collect(Bus::Token token) {
    ++counts[0];
    if(completion_delivery_experiment::collect(arbiter_probe,token,completion))++counts[1];
    else ++counts[2];
}
#if CASE >= 3
static void handle(void*,const Events::Delivery& delivery) {
    if(delivery.has_message)collect(delivery.message);
}
#endif
#endif
// Link/ABI probe only. No startup, vectors, physical peripheral or IRQ backend.
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main() {
    for(;;) {
#if CASE > 0 && !UNUSED
        Bus::Token incoming{argument[0],uint8_t(argument[1]),uint8_t(argument[2])};
        switch(operation) {
        case 0:observed[0]=arbiter_probe.open(argument[0]);break;
        case 1:observed[0]=arbiter_probe.end(argument[0]);break;
        case 2:observed[0]=unsigned(arbiter_probe.submit(argument[0],&buffers_probe[argument[1]%CLIENTS],argument[2],IoMode(argument[3]),argument[4],output));break;
        case 3:observed[0]=arbiter_probe.dispatch(output);break;
        case 4:observed[0]=arbiter_probe.finish(incoming,IoResult(argument[3]),argument[4],uint8_t(argument[5]),argument[6]!=0);break;
        case 5:observed[0]=unsigned(arbiter_probe.cancel(incoming));break;
        case 6:observed[0]=arbiter_probe.notice(incoming);break;
        case 7:observed[0]=arbiter_probe.ack(incoming);break;
        case 8:observed[0]=arbiter_probe.take(incoming,completion);break;
        case 9:observed[0]=buffers_probe[argument[0]%CLIENTS].write(argument[1],uint8_t(argument[2]));break;
        case 10:observed[0]=buffers_probe[argument[0]%CLIENTS].read(argument[1],byte);break;
        case 11:{unsigned i=argument[0]%(Pooled?2:CLIENTS);observed[0]=arbiter_probe.phase(i);observed[1]=arbiter_probe.generation(i);break;}
        case 12:collect(incoming);break;
#if CASE >= 3
        case 13:target_probe=events_probe.add(handle,nullptr,WorkDomain(argument[0]%2),argument[1]!=0);break;
        case 14:observed[0]=events_probe.remove(target_probe);break;
        case 15:events_probe.initialize();break;
        case 16:observed[0]=unsigned(completion_delivery_experiment::pump(arbiter_probe,events_probe,target_probe,incoming));break;
        case 17:{auto result=events_probe.turn(argument[0],WorkOpportunity(argument[1]%3),WorkProfile(argument[2]%3),WorkContext(argument[3]%4));
            observed[0]=unsigned(result.status);observed[1]=result.selected;observed[8]=result.delivered;break;}
#endif
        }
        observed[2]=output.generation;observed[3]=(unsigned(output.index)<<8)|output.client;
        observed[4]=unsigned(completion.result);observed[5]=completion.transferred;observed[6]=completion.digest;
        observed[7]=(arbiter_probe.current()<<24)|(arbiter_probe.cursor()<<16)|byte;
        observed[9]=counts[0];observed[10]=counts[1];observed[11]=counts[2];
#else
        asm volatile("");
#endif
    }
}
