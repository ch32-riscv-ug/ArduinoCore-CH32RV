#include "../../unit/core_redesign/h417_endpoint.hpp"
using namespace h417_transport_experiment;
extern "C" bool transport_gate_try(unsigned);
extern "C" void transport_gate_leave(unsigned);
struct OpaqueGate {
    unsigned slot=0;
    bool try_enter(){return transport_gate_try(slot);}
    void leave(){transport_gate_leave(slot);}
};
using State=Shared<Owners(POLICY),FEATURES>;
using Transport=View<State,OpaqueGate>;
#if POLICY == 2 || (POLICY == 0 && CORE == 0) || (POLICY == 1 && CORE == 1)
#define CONSUMER 1
#else
#define CONSUMER 0
#endif
#if CASE > 0
State shared_probe __attribute__((section(".shared_state")));
OpaqueGate gate_probe;
Transport view_probe(shared_probe,gate_probe);
#if CONSUMER
Endpoint<FAMILY,Transport> endpoint_probe;
#endif
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation,argument[8],observed[24];
Ticket ticket_probe{};
Token token_probe{};
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main() {
#if CASE > 0 && !UNUSED && CONSUMER
    endpoint_probe.begin(view_probe,CORE,experiment::WorkDomain(argument[0]&1),argument[1]!=0);
#endif
    for(;;) {
#if CASE > 0 && !UNUSED
        auto& v=view_probe;
        switch(operation) {
        case 0:observed[0]=unsigned(v.push(CORE,argument[0]&1,argument[1],ticket_probe));break;
        case 1:observed[0]=unsigned(v.reserve(CORE,token_probe));break;
        case 2:observed[0]=unsigned(v.write(CORE,token_probe,argument[0],argument[1]));break;
        case 3:observed[0]=unsigned(v.publish(CORE,token_probe,argument[0]&1,ticket_probe));break;
        case 4:observed[0]=unsigned(v.hint(ticket_probe));break;
        case 5:observed[0]=unsigned(v.hint({argument[0],argument[1],argument[2]&1}));break;
        case 6:observed[0]=unsigned(v.enable(argument[0]&1,argument[1]!=0));break;
        case 7:observed[0]=unsigned(v.recover());break;
        case 8:observed[0]=unsigned(v.dma(argument[0]!=0));break;
        case 9:observed[0]=unsigned(v.attest(argument[0]));break;
        case 10:observed[0]=unsigned(v.restart());break;
#if CONSUMER
        case 11:{auto p=endpoint_probe.opportunity(experiment::WorkOpportunity(argument[0]%3),experiment::WorkProfile(argument[1]%3),experiment::WorkContext(argument[2]%4));observed[0]=unsigned(p.context);observed[1]=unsigned(p.transport);observed[2]=p.delivered;break;}
#endif
        default:break;
        }
        observed[3]=sizeof(shared_probe);observed[4]=sizeof(view_probe);observed[5]=sizeof(gate_probe);
        observed[6]=ticket_probe.epoch;observed[7]=ticket_probe.mask;observed[8]=token_probe.epoch;observed[9]=token_probe.generation;
#if CONSUMER
        auto& e=endpoint_probe;observed[10]=e.borrowed;observed[11]=e.calls;observed[12]=unsigned(e.last_data);
        observed[13]=e.permit.epoch;observed[14]=e.permit.serial;observed[15]=sizeof(e);
#if FEATURES & 1
        observed[16]=e.messages;observed[17]=e.message_hash;
#endif
#if FEATURES & 2
        observed[18]=e.holding;observed[19]=e.cursor;observed[20]=e.bytes;observed[21]=e.blocks;observed[22]=e.block_hash;observed[23]=e.invalidated;
#endif
#endif
#else
        asm volatile("");
#endif
    }
}
