#include "stack_pipeline.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <ucontext.h>
using namespace experiment;
// Host-only execution mechanism for the exact C++ workflow body. Its stack
// allocation and machine context are never used as an MCU size/cycle result.
struct HostFiber {
    ucontext_t main{},fiber{};
    alignas(16) unsigned char stack[65536];
    StackPipeline& app;
    bool running=false;
    static inline HostFiber* current=nullptr;
    static void body() { stack_workflow(current->app,[] {current->yield();}); }
    explicit HostFiber(StackPipeline& a):app(a) {
        assert(!current);current=this;
        assert(getcontext(&fiber)==0);fiber.uc_stack.ss_sp=stack;fiber.uc_stack.ss_size=sizeof(stack);
        fiber.uc_link=nullptr;makecontext(&fiber,body,0);
    }
    void yield() { assert(running);running=false;assert(swapcontext(&fiber,&main)==0);assert(running); }
    void resume() { assert(!running);running=true;assert(swapcontext(&main,&fiber)==0);assert(!running); }
    ~HostFiber(){ assert(!running);current=nullptr; }
};
int main(int argc,char** argv) {
    assert(argc==2);const bool stackful=std::strtoul(argv[1],nullptr,10)!=0;
    StackPipeline app;WorkflowContinuation continuation;HostFiber fiber(app);
    std::string command;
    while(std::cin>>command) {
        int accepted=-1;
        if(command=="input" || command=="complete") { uint32_t value;std::cin>>value;accepted=app.offer(command=="complete",value); }
        else if(command=="turn") { if(stackful)fiber.resume();else continuation.step(app);app.backend(); }
        else { assert(command=="observe"); }
        std::cout<<"["<<accepted<<','<<app.displayed<<','<<app.completed<<','<<app.rejected<<','<<app.mismatch<<','
                 <<unsigned(app.backend_phase)<<','<<app.pending_request<<','<<app.pending_result;
        for(auto& q:app.queues)std::cout<<','<<q.full<<','<<q.value;
        std::cout<<"]\n";
    }
}
