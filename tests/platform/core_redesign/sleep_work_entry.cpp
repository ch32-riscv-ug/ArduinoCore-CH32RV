#include "../../unit/core_redesign/sleep_work.hpp"
using namespace sleep_work_experiment;
#if CASE > 0
Application<FAMILY,CAPACITY,TIMED> application_probe;
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation,argument[8],observed[24];
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main() {
#if CASE > 0 && !UNUSED
    application_probe.begin(WorkDomain::System,false);
#endif
    for(;;) {
#if CASE > 0 && !UNUSED
        auto& a=application_probe;const unsigned i=argument[0]&1;auto& w=a.workers[i];
        switch(operation) {
        case 0:observed[0]=w.submit(argument[1]);break;
        case 1:observed[0]=unsigned(w.port.start(argument[1]));break;
        case 2:observed[0]=w.port.finish(argument[1],IoResult(argument[2]),argument[3],uint8_t(argument[4]),argument[5]!=0);break;
        case 3:observed[0]=unsigned(a.opportunity(i,WorkOpportunity(argument[1]%3),WorkProfile(argument[2]%3),WorkContext(argument[3]%4)));break;
        case 4:observed[0]=a.flow.request(a.attempt);break;
        case 5:observed[0]=unsigned(a.flow.poll());break;
        case 6:observed[0]=unsigned(a.flow.enter(argument[1],argument[2]!=0,argument[3]!=0,Injection(argument[4]%4)));break;
        case 7:observed[0]=a.flow.wake(argument[1]);break;
        case 8:a.flow.post_input();break;
        case 9:observed[0]=a.flow.take_input();break;
        case 10:observed[0]=a.flow.abort();break;
        case 11:observed[0]=a.flow.retry();break;
        case 12:a.source_status=a.flow.restore_clock(argument[1],{argument[3],argument[2],6000000},argument[4]!=0,argument[5],argument[6],a.stamp);observed[0]=unsigned(a.source_status);break;
        case 13:w.port.restoration=Reply(argument[1]%3);break;
        case 14:a.source_status=a.clock.observe({argument[2],argument[1],6000000},{argument[3],true},a.stamp);observed[0]=unsigned(a.source_status);break;
#if TIMED
        case 15:observed[0]=a.timer.arm(a.source_status,a.stamp,argument[1],argument[2]);break;
        case 16:observed[0]=unsigned(a.timer.poll(a.clock.phase()==time_experiment::State::Paused?time_experiment::Result::Paused:a.source_status,a.stamp));break;
        case 17:observed[0]=a.timer.take(a.notice);break;
#endif
        default:break;
        }
        observed[1]=unsigned(a.flow.phase());observed[2]=a.attempt;observed[3]=a.list.frozen();
        observed[4]=a.flow.input_pending();observed[5]=a.flow.clock_ready();observed[6]=uint32_t(a.stamp.us);observed[7]=a.stamp.epoch;
        observed[8]=w.port.powered;observed[9]=w.port.admitting;observed[10]=w.port.io.active();observed[11]=w.port.io.terminal();
        observed[12]=w.port.buffer.available();observed[13]=w.port.io.token();observed[14]=w.remaining;observed[15]=w.completed_steps;
        observed[16]=w.collected;observed[17]=w.calls;observed[18]=unsigned(w.completion.result);observed[19]=w.completion.transferred;
        observed[20]=sizeof(w);observed[21]=sizeof(a.list);observed[22]=sizeof(a.clock);
#if TIMED
        observed[23]=sizeof(a.timer);
#else
        observed[23]=0;
#endif
#else
        asm volatile("");
#endif
    }
}
