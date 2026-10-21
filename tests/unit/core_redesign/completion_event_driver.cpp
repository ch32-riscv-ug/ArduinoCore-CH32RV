#include "completion_arbiter.hpp"
#include "completion_delivery.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace experiment;
// Integration experiment only: serial calls, no inline dispatch or hardware.
// Compare acknowledging at enqueue with retaining the source until delivery.
template<bool Dedicated> struct Fixture {
    using Bus=completion_arbiter_experiment::Arbiter<3,Dedicated?3:2,Dedicated>;
    using Token=typename Bus::Token;
    using Events=EventExecutor<Token,1,1>;
    Bus bus;
    typename Bus::Buffer buffers[3];
    Events events;
    typename Events::Token target=Events::invalid();
    unsigned calls=0,collected=0,skipped=0;
    bool recurse=false;
    TurnStatus recursive=TurnStatus::Ran;
    static void handle(void* context,const typename Events::Delivery& delivery) {
        auto& f=*static_cast<Fixture*>(context);
        ++f.calls;
        assert(delivery.has_message);
        typename Bus::Completion value;
        // The payload is a hint. Revalidate source lifetime, openness and
        // pending state before collection; end() may follow enqueue.
        if(completion_delivery_experiment::collect(f.bus,delivery.message,value)) {
            assert(value.result==IoResult::Success&&value.transferred==4);
            ++f.collected;
        } else ++f.skipped;
        if(f.recurse) {
            f.recursive=f.events.turn(1,WorkOpportunity::Explicit,WorkProfile::U).status;
        }
    }
    void bind(WorkDomain domain=WorkDomain::System,bool opted=false) {
        target=events.add(handle,this,domain,opted);assert(events.valid(target));
    }
    Fixture() { for(unsigned c=0;c<3;++c)assert(bus.open(c)); }
    Token submit(unsigned c) {
        Token t{};
        assert(bus.submit(c,&buffers[c],4,IoMode::Read,42,t)==IoStart::Accepted);
        return t;
    }
    void finish(Token t) {
        Token active{};assert(bus.dispatch(active));
        assert(active.client==t.client&&active.index==t.index&&active.generation==t.generation);
        assert(bus.finish(t,IoResult::Success,4,20,true));
    }
    Token complete(unsigned c) { auto t=submit(c);finish(t);return t; }
    completion_delivery_experiment::Send pump(Token t,bool early_ack=false) {
        assert(bus.notice(t));
        const auto result=completion_delivery_experiment::pump(bus,events,target,t);
        if(result==completion_delivery_experiment::Send::Accepted&&early_ack)assert(bus.ack(t));
        return result;
    }
    typename Events::Turn turn(WorkOpportunity o=WorkOpportunity::Explicit,WorkProfile p=WorkProfile::U,
                              WorkContext c=WorkContext::Normal) { return events.turn(1,o,p,c); }
    void cleanup(Token t) {
        typename Bus::Completion result;assert(bus.take(t,result));
        assert(buffers[t.client].available());
    }
    void report() {
        for(auto& b:buffers)assert(b.available());
        std::printf("{\"calls\":%u,\"collected\":%u,\"skipped\":%u,\"all_buffers_returned\":true}\n",
                    calls,collected,skipped);
    }
};

template<bool D> void run(int argc,char** argv) {
    Fixture<D> f;
    const char* scenario=argv[2];
    if(!std::strcmp(scenario,"matrix")) {
        assert(argc==6);
        const unsigned domain=std::atoi(argv[3]),profile=std::atoi(argv[4]),opportunity=std::atoi(argv[5]);
        assert(domain<3&&profile<3&&opportunity<3);
        f.bind(domain?WorkDomain::User:WorkDomain::System,domain==2);f.events.initialize();
        auto t=f.complete(0);assert(f.pump(t)==completion_delivery_experiment::Send::Accepted);
        const auto turn=f.turn(WorkOpportunity(opportunity),WorkProfile(profile));
        const bool eligible=opportunity==0||(profile!=0&&(domain==0||(profile==2&&domain==2)));
        assert(turn.delivered==unsigned(eligible));
        assert(turn.status==(opportunity&&profile==0?TurnStatus::Suppressed:TurnStatus::Ran));
        if(!eligible) { assert(f.bus.notice(t)&&!f.buffers[0].available());assert(f.turn().delivered==1); }
        assert(f.collected==1);
    } else if(!std::strcmp(scenario,"full")) {
        f.bind();f.events.initialize();
        auto a=f.complete(0);assert(f.pump(a)==completion_delivery_experiment::Send::Accepted);
        auto b=f.complete(1); // Bus progresses while a's result and buffer stay held.
        assert(f.pump(b)==completion_delivery_experiment::Send::Full&&f.bus.notice(b));
        assert(!f.buffers[0].available()&&!f.buffers[1].available());
        assert(f.turn().delivered==1&&f.collected==1);
        assert(f.pump(b)==completion_delivery_experiment::Send::Accepted);assert(f.turn().delivered==1&&f.collected==2);
    } else if(!std::strcmp(scenario,"unbind")) {
        assert(argc==4);const bool early=std::atoi(argv[3]);
        f.bind();f.events.initialize();auto t=f.complete(0);
        assert(f.pump(t,early)==completion_delivery_experiment::Send::Accepted);
        const auto old=f.target;assert(f.events.remove(old));
        assert(!f.buffers[0].available());
        f.bind();assert(f.target.generation!=old.generation);
        assert(f.events.send(old,t)==EventSend::Stale);
        assert(f.turn().delivered==0);
        if(early) {
            // Counterexample: accepted into a queue is not delivered. A lost
            // hint leaves a collectable result but no pending source notice.
            assert(!f.bus.notice(t));f.cleanup(t);
        } else {
            assert(f.bus.notice(t));assert(f.pump(t)==completion_delivery_experiment::Send::Accepted);
            assert(f.turn().delivered==1&&f.collected==1);
        }
    } else if(!std::strcmp(scenario,"end")) {
        f.bind();f.events.initialize();auto t=f.complete(0);
        assert(f.pump(t)==completion_delivery_experiment::Send::Accepted);assert(f.bus.end(0));
        assert(!f.bus.notice(t)&&!f.buffers[0].available());
        assert(f.turn().delivered==1&&f.skipped==1&&f.collected==0);
        assert(!f.bus.open(0));f.cleanup(t);assert(f.bus.open(0));
    } else if(!std::strcmp(scenario,"stale")) {
        f.bind();f.events.initialize();auto old=f.complete(0);
        assert(f.pump(old)==completion_delivery_experiment::Send::Accepted);f.cleanup(old);
        auto next=f.complete(0);assert(next.generation!=old.generation);
        assert(f.pump(next)==completion_delivery_experiment::Send::Full&&f.bus.notice(next));
        assert(f.turn().delivered==1&&f.skipped==1&&!f.buffers[0].available());
        assert(f.pump(next)==completion_delivery_experiment::Send::Accepted);assert(f.turn().delivered==1&&f.collected==1);
    } else if(!std::strcmp(scenario,"reentrant")) {
        f.bind();f.events.initialize();f.recurse=true;auto t=f.complete(0);
        assert(f.pump(t)==completion_delivery_experiment::Send::Accepted);assert(f.turn().delivered==1);
        assert(f.recursive==TurnStatus::Reentrant&&f.collected==1);
    } else if(!std::strcmp(scenario,"rejects")) {
        f.bind();f.events.initialize();auto t=f.submit(0);
        assert(completion_delivery_experiment::pump(f.bus,f.events,f.target,t)==completion_delivery_experiment::Send::NotPending);
        typename Fixture<D>::Bus::Completion sentinel{IoResult::Failure,99,100};
        assert(!completion_delivery_experiment::collect(f.bus,t,sentinel));
        assert(sentinel.result==IoResult::Failure&&sentinel.transferred==99&&sentinel.digest==100);
        f.finish(t);assert(f.events.remove(f.target));
        assert(f.pump(t)==completion_delivery_experiment::Send::Stale&&f.bus.notice(t));
        f.bind();assert(f.pump(t)==completion_delivery_experiment::Send::Accepted);
        assert(f.turn().delivered==1&&f.collected==1);
        assert(!completion_delivery_experiment::collect(f.bus,t,sentinel));
        assert(sentinel.result==IoResult::Failure&&sentinel.transferred==99&&sentinel.digest==100);
    } else if(!std::strcmp(scenario,"context")) {
        assert(argc==4);const unsigned context=std::atoi(argv[3]);assert(context<4);
        f.bind();if(context)f.events.initialize();auto t=f.complete(0);
        assert(f.pump(t)==completion_delivery_experiment::Send::Accepted);
        const auto result=f.turn(WorkOpportunity::Explicit,WorkProfile::U,WorkContext(context));
        assert(result.delivered==0&&f.bus.notice(t)&&!f.buffers[0].available());
        assert(result.status==(context==0?TurnStatus::NotInitialized:context==2?TurnStatus::Reentrant:TurnStatus::Forbidden));
        if(!context)f.events.initialize();
        assert(f.turn().delivered==1&&f.collected==1);
    } else assert(false);
    f.report();
}
int main(int argc,char** argv) {
    assert(argc>=3);
    if(!std::strcmp(argv[1],"dedicated"))run<true>(argc,argv);
    else { assert(!std::strcmp(argv[1],"pooled"));run<false>(argc,argv); }
}
