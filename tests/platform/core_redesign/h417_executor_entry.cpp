#include "../../unit/core_redesign/h417_executor.hpp"
using namespace h417_executor_experiment;
#if CASE > 0
Application<FAMILY,Owners(POLICY),FEATURES> application_probe;
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation,argument[8],observed[28];
Token slots_probe[2]{};
Ticket ticket_probe{};
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main() {
#if CASE > 0 && !UNUSED
    application_probe.begin(WorkDomain(argument[0]&1),argument[1]!=0);
#endif
    for(;;) {
#if CASE > 0 && !UNUSED
        auto& a=application_probe;const unsigned core=argument[0]&1,index=argument[1]&1;auto& w=a.workers[core];
        switch(operation) {
        case 0:observed[0]=a.push(core,index,argument[2],ticket_probe);break;
        case 1:observed[0]=a.reserve(core,slots_probe[index]);break;
        case 2:observed[0]=a.write(core,slots_probe[index],argument[2],argument[3]);break;
        case 3:observed[0]=a.publish(core,slots_probe[index],argument[2]&1,ticket_probe);break;
        case 4:observed[0]=a.hint(ticket_probe);break;
        case 5:observed[0]=a.hint({argument[1],argument[2],core});break;
        case 6:observed[0]=unsigned(w.opportunity(WorkOpportunity(argument[1]%3),WorkProfile(argument[2]%3),WorkContext(argument[3]%4)));break;
        case 7:a.gate.blocked=argument[1]!=0;break;
        case 8:w.online=argument[1]!=0;break;
        case 9:observed[0]=a.recover();break;
        case 10:a.dma_quiet=argument[1]!=0;break;
        case 11:observed[0]=a.attest(argument[1]);break;
        case 12:observed[0]=a.restart();break;
        default:break;
        }
        observed[1]=a.epoch;observed[2]=a.recovering;observed[3]=a.hints[core];observed[4]=w.online;observed[5]=w.calls;observed[6]=w.busy;
#if FEATURES & 1
        observed[7]=w.messages;observed[8]=w.message_hash;
#endif
#if FEATURES & 2
        observed[9]=w.holding;observed[10]=w.cursor;observed[11]=w.bytes;observed[12]=w.blocks;observed[13]=w.block_hash;
        observed[14]=w.invalidated;observed[15]=w.lease.epoch;observed[16]=w.lease.generation;observed[17]=w.lease.index;
#endif
        observed[18]=sizeof(a);observed[19]=sizeof(w);observed[20]=sizeof(a.fabric);observed[21]=sizeof(a.gate);
        observed[22]=ticket_probe.epoch;observed[23]=ticket_probe.mask;observed[24]=ticket_probe.destination;
        observed[25]=slots_probe[index].epoch;observed[26]=slots_probe[index].generation;observed[27]=slots_probe[index].index;
#else
        asm volatile("");
#endif
    }
}
