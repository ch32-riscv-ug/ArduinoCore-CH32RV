#include "../../unit/core_redesign/reentry_depth.hpp"
#if CASE > 0
experiment::ReentryDepth<CAPACITY,(FAMILY>1)> application_probe;
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation,argument[4],observed[12];
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main() {
    for (;;) {
#if CASE > 0 && !UNUSED
        auto& app=application_probe;
        const unsigned index=argument[0]%5;
        switch(operation%6) {
        case 0:app.nodes[index]={int8_t(int(argument[1]%6)-1),int8_t(int(argument[2]%3)-1),int8_t(int(argument[3]%3)-1),experiment::WaitProducer::System,false};break;
        case 1:observed[0]=unsigned(app.run(index,experiment::WorkContext(argument[1]%4)));break;
        case 2:app.nodes[index].kind=experiment::WaitProducer(argument[1]%4);app.nodes[index].opted=argument[2]&1;break;
        case 3:observed[0]=app.external(index);break;
        case 4:app.owner[argument[0]%2]=int8_t(int(argument[1]%7)-2);break;
        default:app.permission={experiment::WorkOpportunity(argument[0]%3),experiment::WorkProfile(argument[1]%3),bool(argument[2]&1),bool(argument[2]&2),bool(argument[2]&4),true};break;
        }
        observed[1]=app.done;observed[2]=app.depth;observed[3]=app.peak;observed[4]=app.active;
        observed[5]=app.calls;observed[6]=app.limited;observed[7]=app.active_hits;observed[8]=app.held_hits;
        observed[9]=uint32_t(app.owner[0]);observed[10]=uint32_t(app.owner[1]);observed[11]=app.configured();
#else
        asm volatile("");
#endif
    }
}
