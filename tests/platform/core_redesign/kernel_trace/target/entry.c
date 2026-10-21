#include "FreeRTOSConfig.h"
#include "../fixture.h"
#if EXP_USED
volatile uint32_t command_sequence,reply_sequence,operation,argument,observation[16],fixture_fault;
void vExperimentAssert(void) { fixture_fault=1;for(;;)__asm volatile(""); }
static void driver(void *unused) {
    (void)unused;uint32_t processed=0;
    for(;;) {
        const uint32_t sequence=command_sequence;
        if(sequence==processed)continue;
        int accepted=-1;
        switch(operation) {
        case 0:accepted=fixture_offer(0,argument);break;
        case 1:accepted=fixture_offer(1,argument);break;
        case 2:fixture_drain();break;
        case 3:break;
        default:vExperimentAssert();
        }
        uint32_t snapshot[16];fixture_snapshot(accepted,snapshot);
        for(unsigned i=0;i<16;++i)observation[i]=snapshot[i];
        processed=sequence;reply_sequence=sequence;
    }
}
extern void SysTick_Handler(void),SW_Handler(void);
void (* const fixture_entries[])(void) __attribute__((used,section(".fixture_roots")))={SysTick_Handler,SW_Handler};
#endif
void probe_main(void) __attribute__((section(".text.entry"),noreturn));
void probe_main(void) {
#if EXP_USED
    fixture_start(driver);
#endif
    for(;;)__asm volatile("");
}
