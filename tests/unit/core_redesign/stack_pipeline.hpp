#pragma once
#include <stdint.h>
namespace experiment {
// Single-core serialized queues. Neither this fixture nor its context switch
// is callable from IRQ or another core. No implicit event/clock progress.
struct PipelineSlot {
    uint32_t value=0;
    bool full=false;
    bool push(uint32_t v) { if(full)return false;value=v;full=true;return true; }
    bool pop(uint32_t& v) { if(!full)return false;v=value;full=false;return true; }
};
struct StackPipeline {
    enum { Input,Request,RawComplete,Complete };
    PipelineSlot queues[4];
    uint32_t displayed=0,completed=0,rejected=0,mismatch=0;
    uint32_t pending_request=0,pending_result=0;
    uint8_t backend_phase=0;
    bool offer(bool completion,uint32_t value) {
        const bool accepted=queues[completion?RawComplete:Input].push(value);
        if(!accepted)++rejected;
        return accepted;
    }
    void show(uint32_t value) { displayed=value;++completed; }
    void backend() {
        for (;;) {
            if(backend_phase==0) {
                if(!queues[Request].pop(pending_request))return;
                backend_phase=1;
            }
            if(backend_phase==1) {
                if(!queues[RawComplete].pop(pending_result))return;
                if(pending_request!=pending_result)++mismatch;
                backend_phase=2;
            }
            if(!queues[Complete].push(pending_result))return;
            backend_phase=0;
        }
    }
};
struct WorkflowContinuation {
    uint32_t pending=0;
    uint8_t phase=0;
    void step(StackPipeline& app) {
        for (;;) {
            if(phase==0) {
                if(!app.queues[StackPipeline::Input].pop(pending))return;
                phase=1;
            }
            if(phase==1) {
                if(!app.queues[StackPipeline::Request].push(pending))return;
                phase=2;
            }
            uint32_t value=0;
            if(!app.queues[StackPipeline::Complete].pop(value))return;
            app.show(value);phase=0;return;
        }
    }
};
// The same protocol expressed with local variables retained across yields.
// Yield returns only after a later explicit resume. No independent task list.
template<class Yield> [[noreturn]] void stack_workflow(StackPipeline& app,Yield yield) {
    for (;;) {
        uint32_t request=0;
        while(!app.queues[StackPipeline::Input].pop(request))yield();
        while(!app.queues[StackPipeline::Request].push(request))yield();
        uint32_t result=0;
        while(!app.queues[StackPipeline::Complete].pop(result))yield();
        app.show(result);
        yield();
    }
}
} // namespace experiment
