#include "../../unit/core_redesign/i2c_managed_handoff.hpp"
#include "../../unit/core_redesign/completion_delivery.hpp"
// Structural link probe only. Caller precondition is IRQ-off, exclusive
// peripheral/clock/pins. These barriers do not implement an IRQ lock.
struct Bus {
    static uint16_t read16(uintptr_t p){return *reinterpret_cast<volatile uint16_t*>(p);}
    static void write16(uintptr_t p,uint16_t v){*reinterpret_cast<volatile uint16_t*>(p)=v;}
    static unsigned critical_enter(){asm volatile("":::"memory");return 0;}
    static void critical_leave(unsigned){asm volatile("":::"memory");}
};
constexpr unsigned Records=CASE==2?2:3;
using Core=i2c_handoff_experiment::Manager<Bus,3,Records,CASE!=2,BUFFER_BYTES>;
using Domain=i2c_resource_experiment::Domain<2>;
struct Pins {bool valid=false;bool ready() const{return valid;}};
#if MANAGED
using Queue=i2c_managed_handoff_experiment::Managed<Core,Domain>;
#if CASE > 0
Domain resources_probe;Pins pins_probe;
#endif
#else
using Queue=Core;
#endif
using Events=experiment::EventExecutor<Queue::Token,2,1>;
#if CASE > 0 && EVENTS
Events events_probe;Events::Token targets_probe[2]{};
#endif
#if CASE > 0
Queue managers_probe[2];
Queue::Storage buffers_probe[3];
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation,argument[8],observed[12],counts[2][3];
Queue::Token output{};Queue::Completion completion{};uint8_t byte=0;
static void collect(Queue& owner,Queue::Token token){
    const unsigned source=unsigned(&owner-managers_probe);++counts[source][0];
    if(completion_delivery_experiment::collect(owner,token,completion))++counts[source][1];
    else ++counts[source][2];
}
#if EVENTS
static void handle(void* context,const Events::Delivery& delivery){
    if(delivery.has_message)collect(*static_cast<Queue*>(context),delivery.message);
}
#endif
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main(){
    for(;;){
#if CASE > 0 && !UNUSED
        const unsigned source=argument[7]%2;auto& manager=managers_probe[source];
        Queue::Token incoming{argument[0],uint8_t(argument[1]),uint8_t(argument[2])};
        switch(operation){
        case 0:
#if MANAGED
            observed[0]=unsigned(manager.open_backend(resources_probe,pins_probe,argument[2]));
#else
            observed[0]=unsigned(manager.open_backend(argument[0]!=0,argument[1],argument[2]));
#endif
            break;
        case 1:observed[0]=manager.close_backend();break;
        case 2:observed[0]=manager.open(argument[0]);break;
        case 3:observed[0]=manager.end(argument[0]);break;
        case 4:observed[0]=unsigned(manager.submit(argument[0],&buffers_probe[argument[1]%3],argument[2],argument[3],argument[4],output));break;
        case 5:observed[0]=unsigned(manager.dispatch(output));break;
        case 6:manager.step();break;
        case 7:observed[0]=manager.reap();break;
        case 8:observed[0]=unsigned(manager.cancel(incoming));break;
        case 9:observed[0]=manager.notice(incoming);break;
        case 10:observed[0]=manager.ack(incoming);break;
        case 11:observed[0]=manager.take(incoming,completion);break;
        case 12:observed[0]=buffers_probe[argument[0]%3].write(argument[1],uint8_t(argument[2]));break;
        case 13:observed[0]=buffers_probe[argument[0]%3].read(argument[1],byte);break;
        case 14:{unsigned i=argument[0]%Records;observed[0]=manager.phase(i);observed[11]=manager.generation(i);break;}
#if MANAGED
        case 15:observed[0]=resources_probe.begin_change();break;
        case 16:observed[0]=resources_probe.publish(argument[0]);break;
        case 17:pins_probe.valid=argument[0]!=0;break;
        case 18:observed[0]=resources_probe.frequency();observed[11]=(resources_probe.count()<<16)|resources_probe.held();break;
#endif
        case 19:collect(manager,incoming);break;
#if EVENTS
        case 20:events_probe.initialize();break;
        case 21:targets_probe[source]=events_probe.add(handle,&manager,experiment::WorkDomain(argument[0]%2),argument[1]!=0);break;
        case 22:observed[0]=events_probe.remove(targets_probe[source]);break;
        case 23:observed[0]=unsigned(completion_delivery_experiment::pump(manager,events_probe,targets_probe[source],incoming));break;
        case 24:{auto turn=events_probe.turn(argument[0]%3,experiment::WorkOpportunity(argument[1]%3),experiment::WorkProfile(argument[2]%3),experiment::WorkContext(argument[3]%4));
            observed[0]=unsigned(turn.status);observed[11]=(turn.selected<<16)|turn.delivered;break;}
#endif
        }
        observed[1]=manager.backend_phase();observed[2]=manager.backend_generation();
        observed[3]=(uint32_t(byte)<<24)|(manager.current()<<16)|manager.cursor();
        observed[4]=output.generation;observed[5]=(unsigned(output.index)<<8)|output.client;
        observed[6]=unsigned(completion.result);observed[7]=(uint32_t(completion.issued)<<16)|completion.confirmed;
        observed[8]=(uint32_t(completion.received)<<16)|completion.flags;observed[9]=counts[source][0];observed[10]=counts[source][1];observed[11]=counts[source][2];
#else
        asm volatile("");
#endif
    }
}
