#include "FreeRTOS.h"
#include "task.h"
#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>

/* Test mechanism only: fixed native contexts, no host threads or real ticks.
 * This is not the WCH port and supplies no MCU context/cycle/RAM evidence. */
enum { ContextCount=5 };
typedef struct {
    ucontext_t cpu;
    TaskFunction_t entry;
    void *argument;
    void *base;
    size_t bytes;
} NativeContext;
static NativeContext contexts[ContextCount];
static unsigned registered,initialized,mask_depth,switches;
static int pending;
extern void * volatile pxCurrentTCB;

void experiment_host_register_stack(void *base,size_t bytes) {
    assert(registered<ContextCount);
    contexts[registered].base=base;contexts[registered].bytes=bytes;++registered;
}
static NativeContext *selected(void) {
    assert(pxCurrentTCB);
    return (NativeContext *)*(StackType_t **)pxCurrentTCB;
}
static void body(int index) {
    contexts[index].entry(contexts[index].argument);
    abort();
}
StackType_t *pxPortInitialiseStack(StackType_t *top,TaskFunction_t entry,void *argument) {
    for(unsigned i=0;i<registered;++i) {
        NativeContext *c=&contexts[i];
        if((uintptr_t)top>=(uintptr_t)c->base && (uintptr_t)top<(uintptr_t)c->base+c->bytes) {
            assert(!c->entry);c->entry=entry;c->argument=argument;
            assert(getcontext(&c->cpu)==0);c->cpu.uc_stack.ss_sp=c->base;c->cpu.uc_stack.ss_size=c->bytes;c->cpu.uc_link=NULL;
            makecontext(&c->cpu,(void (*)(void))body,1,(int)i);++initialized;
            return (StackType_t *)c;
        }
    }
    abort();
}
void experiment_host_prepare_drain(void) { switches=0; }
static void dispatch(void) {
    assert(!mask_depth);pending=0;
    assert(++switches<=256);
    NativeContext *old=selected();vTaskSwitchContext();NativeContext *next=selected();
    if(old!=next)assert(swapcontext(&old->cpu,&next->cpu)==0);
}
void experiment_host_yield(void) { if(mask_depth)pending=1;else dispatch(); }
void experiment_host_enter(void) { ++mask_depth; }
void experiment_host_leave(void) { assert(mask_depth);if(!(--mask_depth) && pending)dispatch(); }
void experiment_host_enable(void) { mask_depth=0;if(pending)dispatch(); }
UBaseType_t experiment_host_mask(void) { unsigned saved=mask_depth;++mask_depth;return saved; }
void experiment_host_restore(UBaseType_t saved) { assert(saved<mask_depth);mask_depth=(unsigned)saved;if(!mask_depth && pending)dispatch(); }
BaseType_t xPortStartScheduler(void) {
    assert(initialized==registered);mask_depth=0;pending=0;
    setcontext(&selected()->cpu);abort();
}
void vPortEndScheduler(void) { abort(); }
