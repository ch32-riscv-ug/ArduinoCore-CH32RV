#include "../../../unit/core_redesign/stack_pipeline.hpp"
#include "context.h"
#if EXP_USED
experiment::StackPipeline application_probe;
#if EXP_STACKFUL
ExperimentContext main_context,fiber_context;
alignas(16) uint8_t fiber_stack[EXP_STACK_BYTES];
static void yield() { experiment_context_swap(&fiber_context,&main_context); }
[[noreturn]] static void fiber_entry() { experiment::stack_workflow(application_probe,yield); }
#else
experiment::WorkflowContinuation continuation_probe;
#endif
volatile uint32_t operation,argument,observed[8];
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main() {
#if EXP_USED && EXP_STACKFUL
    fiber_context.ra=uint32_t(reinterpret_cast<uintptr_t>(fiber_entry));
    fiber_context.sp=uint32_t(reinterpret_cast<uintptr_t>(fiber_stack+sizeof(fiber_stack)));
#endif
    for (;;) {
#if EXP_USED
        auto& app=application_probe;
        if(operation<2)observed[0]=app.offer(operation!=0,argument);
        else {
#if EXP_STACKFUL
            experiment_context_swap(&main_context,&fiber_context);
#else
            continuation_probe.step(app);
#endif
            app.backend();
        }
        observed[1]=app.displayed;observed[2]=app.completed;observed[3]=app.rejected;observed[4]=app.mismatch;
        observed[5]=app.backend_phase;observed[6]=app.pending_request;observed[7]=app.pending_result;
#else
        asm volatile("");
#endif
    }
}
