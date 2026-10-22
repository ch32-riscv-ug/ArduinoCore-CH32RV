#if CASE == 2
#include "../../../planning/core-redesign/tasks/EXP-07/disconnect-change/after.cpp"
#else
#include "../../../planning/core-redesign/tasks/EXP-07/disconnect-change/before.cpp"
#endif
#if CASE > 0
ExerciseApplication<FAMILY,TOUCH> application_probe;
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation,argument[6],observed[24];
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main(){
#if CASE > 0 && !UNUSED
    application_probe.begin();
#endif
    for(;;){
#if CASE > 0 && !UNUSED
        auto& a=application_probe;
        switch(operation){
        case 0:observed[0]=unsigned(a.pps.attach());break;
        case 1:a.pps.detach();break;
        case 2:observed[0]=a.pps.enqueue(int16_t(argument[0]));break;
        case 3:a.hardware(argument[0]);break;
        case 4:a.source_mv=int32_t(argument[0]);a.source_ma=int32_t(argument[1]);break;
        case 5:a.bus.fail_next[argument[0]%(TOUCH?3:2)+1]=true;break;
        case 6:a.pps.completion(argument[0],argument[1],argument[2]!=0);break;
        case 7:a.pps.auto_hardware=argument[0]==0;break;
        case 8:observed[0]=a.pps.finish(argument[0],argument[1],experiment::IoResult(argument[2]),argument[3],argument[4]!=0);break;
        case 9:observed[0]=a.pps.buffer.write(argument[0],uint8_t(argument[1]));break;
        default:observed[0]=unsigned(a.opportunity(experiment::WorkOpportunity(argument[0]%3),experiment::WorkProfile(argument[1]%3),experiment::WorkContext(argument[2]%4)));break;
        }
        a.changed();observed[1]=uint32_t(a.pps.desired);observed[2]=uint32_t(a.pps.confirmed);observed[3]=a.pps.valid;
        observed[4]=a.pps.accepted_count;observed[5]=a.pps.rejected_count;observed[6]=a.pps.started;observed[7]=a.pps.reclaimed;
        observed[8]=a.bus.started;observed[9]=a.bus.reclaimed;observed[10]=a.bus.corruptions;observed[11]=a.screen.displayed_hash;
        observed[12]=a.measurement_done;observed[13]=a.touch_count();observed[14]=a.pps.inbox.size();observed[15]=sizeof(a);
        observed[16]=a.pps.epoch();observed[17]=a.pps.request_epoch();observed[18]=a.pps.io.opened();observed[19]=a.pps.io.active();
        observed[20]=a.pps.io.cancel_pending();observed[21]=a.pps.buffer.available();observed[22]=a.pps.discarded;observed[23]=a.pps.last.digest;
#else
        asm volatile("");
#endif
    }
}
