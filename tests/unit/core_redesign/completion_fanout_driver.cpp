#include "completion_arbiter.hpp"
#include "completion_delivery.hpp"
#include "completion_fanout.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace experiment;
using completion_fanout_experiment::Reserve;
using completion_fanout_experiment::Send;
template<bool D,unsigned N,unsigned C> struct Fixture {
    using Bus=completion_arbiter_experiment::Arbiter<3,D?3:2,D>;
    using Token=typename Bus::Token;
    struct Value {Token request{};typename Bus::Completion result{};};
    using Pub=completion_fanout_experiment::Publication<Value,N,C>;
    using Events=typename Pub::Events;
    struct Context {Fixture* fixture=nullptr;unsigned id=0;};
    Bus bus;
    typename Bus::Buffer buffers[3];
    Pub publication;
    Events events;
    typename Pub::Target targets[N];Context contexts[N];
    typename Pub::Ticket ticket=0;
    Value expected{};
    unsigned receipts=0,rejected=0,seen[N]{};
    bool replace_later=false;
    static constexpr unsigned all=(1u<<N)-1;
    static void handler(void* context,const typename Events::Delivery& delivery) {
        auto& c=*static_cast<Context*>(context);auto& f=*c.fixture;
        Value value{};
        if(!f.publication.receive(delivery,value)){++f.rejected;return;}
        assert(value.request.generation==f.expected.request.generation&&value.request.index==f.expected.request.index&&value.request.client==f.expected.request.client);
        assert(value.result.result==IoResult::Success&&value.result.transferred==4&&value.result.digest==f.expected.result.digest);
        ++f.receipts;++f.seen[c.id];
        if(f.replace_later&&c.id==0&&N>1) {
            f.replace_later=false;const auto old=f.targets[N-1];assert(f.events.remove(old));f.bind(N-1);
            assert(f.publication.rebind(f.events,f.ticket,N-1,f.targets[N-1]));
            assert(f.publication.pump(f.events,f.ticket,N-1)==Send::Accepted);
            assert(f.events.turn(N,WorkOpportunity::Explicit,WorkProfile::U).status==TurnStatus::Reentrant);
        }
    }
    void bind(unsigned i) {
        targets[i]=events.add(handler,&contexts[i],WorkDomain::User,true);assert(events.valid(targets[i]));
    }
    Fixture() {
        for(unsigned i=0;i<3;++i)assert(bus.open(i));
        for(unsigned i=0;i<N;++i){contexts[i]={this,i};bind(i);}
        events.initialize();
    }
    Token submit(unsigned client) {
        Token t{};assert(bus.submit(client,&buffers[client],4,IoMode::Read,42,t)==IoStart::Accepted);return t;
    }
    void finish(Token t) {
        Token active{};assert(bus.dispatch(active));
        assert(active.generation==t.generation&&active.index==t.index&&active.client==t.client);
        assert(bus.finish(t,IoResult::Success,4,uint8_t(20+t.client),true));
    }
    Token complete(unsigned client) {auto t=submit(client);finish(t);return t;}
    bool capture(Token t) {
        typename Pub::Ticket next=123;
        auto result=publication.reserve(targets,all,next);
        if(result!=Reserve::Accepted){assert(next==123);return false;}
        typename Bus::Completion value;
        if(!completion_delivery_experiment::collect(bus,t,value)){assert(publication.abandon(next));return false;}
        ticket=next;expected={t,value};assert(publication.publish(ticket,expected));
        assert(buffers[t.client].available());uint32_t hash=2166136261u;
        for(unsigned i=0;i<4;++i){uint8_t byte=0;assert(buffers[t.client].read(i,byte)&&byte==20+t.client+i);hash=(hash^byte)*16777619u;}
        assert(value.digest==hash);return true;
    }
    void pump_all() {for(unsigned i=0;i<N;++i)assert(publication.pump(events,ticket,i)==Send::Accepted);}
    auto turn(unsigned limit=N) {return events.turn(limit,WorkOpportunity::Explicit,WorkProfile::U);}
    void report() {
        assert(publication.phase()==0);
        for(auto& b:buffers)assert(b.available());
        for(unsigned i=0;i<N;++i)assert(events.queued(targets[i])==0);
        std::printf("{\"receipts\":%u,\"rejected\":%u,\"all_buffers_returned\":true,\"publication_released\":true}\n",receipts,rejected);
    }
};

template<bool D,unsigned N,unsigned C> void run(const char* scenario,unsigned mask) {
    Fixture<D,N,C> f;using F=Fixture<D,N,C>;using Pub=typename F::Pub;
    assert(mask<=F::all);
    if(!std::strcmp(scenario,"hint")) {
        using Events=EventExecutor<typename F::Token,N,C>;
        Events events;events.initialize();auto t=f.complete(0);
        auto handler=[](void* context,const typename Events::Delivery& delivery) {
            auto& fixture=*static_cast<F*>(context);typename F::Bus::Completion value;
            if(completion_delivery_experiment::collect(fixture.bus,delivery.message,value))++fixture.receipts;
            else ++fixture.rejected;
        };
        for(unsigned i=0;i<N;++i){auto target=events.add(handler,&f,WorkDomain::System);assert(events.send(target,t)==EventSend::Accepted);}
        assert(events.turn(N,WorkOpportunity::Explicit,WorkProfile::U).delivered==N);
        assert(f.receipts==1&&f.rejected==N-1);
    } else if(!std::strcmp(scenario,"full")) {
        assert(f.capture(f.complete(0)));
        for(unsigned i=0;i<N;++i)if(mask&(1u<<i))for(unsigned j=0;j<C;++j)assert(f.events.send(f.targets[i],typename Pub::Payload{})==EventSend::Accepted);
        for(unsigned i=0;i<N;++i) {
            auto result=f.publication.pump(f.events,f.ticket,i);
            assert(result==((mask&(1u<<i))?Send::Full:Send::Accepted));
            if(result==Send::Accepted)assert(f.publication.pump(f.events,f.ticket,i)==Send::Queued);
        }
        for(unsigned step=0;step<N*(C+2);++step) {
            f.turn(1);
            for(unsigned i=0;i<N;++i) {
                auto result=f.publication.pump(f.events,f.ticket,i);
                assert(result==Send::Accepted||result==Send::Full||result==Send::Queued||result==Send::Delivered);
            }
        }
        assert(f.publication.delivered()==F::all&&f.receipts==N);
        for(unsigned i=0;i<N;++i)assert(f.seen[i]==1);
        assert(f.publication.release(f.ticket));
    } else if(!std::strcmp(scenario,"remove")) {
        assert(f.capture(f.complete(0)));f.pump_all();
        for(unsigned i=0;i<N;++i) {
            if(mask&(1u<<i)) {auto old=f.targets[i];assert(f.events.remove(old));f.bind(i);assert(old.generation!=f.targets[i].generation);}
            assert(f.publication.rebind(f.events,f.ticket,i,f.targets[i])==bool(mask&(1u<<i)));
            assert(f.publication.pump(f.events,f.ticket,i)==((mask&(1u<<i))?Send::Accepted:Send::Queued));
        }
        assert(f.turn().delivered==N&&f.receipts==N);
        assert(f.publication.release(f.ticket));
    } else if(!std::strcmp(scenario,"during-turn")) {
        assert(N>1);assert(f.capture(f.complete(0)));f.pump_all();f.replace_later=true;
        assert(f.turn().delivered==N-1&&f.receipts==N-1);
        assert(f.publication.delivered()==(F::all&~(1u<<(N-1))));
        assert(f.turn().delivered==1&&f.receipts==N);assert(f.publication.release(f.ticket));
    } else if(!std::strcmp(scenario,"retained")) {
        assert(f.capture(f.complete(0)));auto next=f.complete(1);
        assert(!f.capture(next)&&f.bus.notice(next)&&!f.buffers[1].available());
        f.pump_all();assert(f.turn().delivered==N);
        assert(!f.capture(next)&&f.bus.notice(next)); // Explicit release is still required.
        assert(f.publication.release(f.ticket));assert(f.capture(next));f.pump_all();
        assert(f.turn().delivered==N&&f.receipts==2*N);assert(f.publication.release(f.ticket));
    } else if(!std::strcmp(scenario,"stale")) {
        assert(f.capture(f.complete(0)));auto old=f.ticket;auto oldvalue=f.expected;
        f.pump_all();assert(f.turn().delivered==N);assert(f.publication.release(old));
        assert(f.capture(f.complete(0))&&f.ticket!=old);
        for(unsigned i=0;i<N;++i) {
            assert(f.events.send(f.targets[i],typename Pub::Payload{old,uint8_t(i),oldvalue})==EventSend::Accepted);
            assert(f.publication.pump(f.events,f.ticket,i)==(C==1?Send::Full:Send::Accepted));
        }
        assert(f.turn().delivered==N&&f.rejected==N&&f.receipts==N);
        assert(f.publication.delivered()==0);
        for(unsigned i=0;i<N;++i)assert(f.publication.pump(f.events,f.ticket,i)==(C==1?Send::Accepted:Send::Queued));
        assert(f.turn().delivered==N&&f.receipts==2*N);assert(f.publication.release(f.ticket));
    } else if(!std::strcmp(scenario,"invalid")) {
        typename Pub::Ticket out=99;
        assert(f.publication.reserve(nullptr,F::all,out)==Reserve::Invalid&&out==99);
        assert(f.publication.reserve(f.targets,0,out)==Reserve::Invalid&&out==99);
        assert(f.publication.reserve(f.targets,F::all+1,out)==Reserve::Invalid&&out==99);
        auto t=f.submit(0);assert(!f.capture(t)&&f.publication.phase()==0&&!f.buffers[0].available());
        f.finish(t);assert(f.capture(t));assert(f.publication.generation()==2);
        assert(f.publication.pump(f.events,1,0)==Send::NotReady);
        assert(f.publication.pump(f.events,f.ticket,N)==Send::Invalid);
        assert(!f.publication.abandon(f.ticket)&&!f.publication.publish(f.ticket,f.expected)&&!f.publication.release(f.ticket));
        typename F::Value sentinel{};sentinel.result.transferred=99;
        typename F::Events::Delivery bad{};bad.has_message=true;bad.target=f.targets[0];bad.message={f.ticket,uint8_t(N),f.expected};
        assert(!f.publication.receive(bad,sentinel)&&sentinel.result.transferred==99);
        assert(f.events.notify(f.targets[0],1));assert(f.turn().delivered==1&&f.rejected==1);
        f.pump_all();
        bad.message={f.ticket,0,f.expected};bad.target=f.targets[0];++bad.target.generation;
        assert(!f.publication.receive(bad,sentinel)&&sentinel.result.transferred==99&&f.publication.queued()==F::all);
        assert(f.turn().delivered==N);assert(f.publication.release(f.ticket));
    } else if(!std::strcmp(scenario,"subset")) {
        assert(mask);auto t=f.complete(0);typename F::Bus::Completion result;
        assert(f.publication.reserve(f.targets,mask,f.ticket)==Reserve::Accepted);
        assert(completion_delivery_experiment::collect(f.bus,t,result));
        f.expected={t,result};assert(f.publication.publish(f.ticket,f.expected));
        unsigned selected=0;
        for(unsigned i=0;i<N;++i) {
            const bool included=mask&(1u<<i);selected+=included;
            assert(f.publication.pump(f.events,f.ticket,i)==(included?Send::Accepted:Send::Invalid));
        }
        assert(f.turn().delivered==selected&&f.receipts==selected);
        assert(f.publication.delivered()==mask&&f.publication.release(f.ticket));
    } else if(!std::strcmp(scenario,"end-after-copy")) {
        assert(f.capture(f.complete(0)));assert(f.bus.end(0));assert(f.buffers[0].write(0,99));f.pump_all();
        assert(f.turn().delivered==N&&f.receipts==N);assert(f.publication.release(f.ticket));
    } else if(!std::strcmp(scenario,"end-before-copy")) {
        auto t=f.complete(0);assert(f.bus.end(0));assert(!f.capture(t));
        assert(!f.buffers[0].available());typename F::Bus::Completion result;
        assert(f.bus.take(t,result)&&f.buffers[0].available());
    } else if(!std::strcmp(scenario,"stalled")) {
        assert(f.capture(f.complete(0)));f.pump_all();assert(f.events.remove(f.targets[N-1]));
        assert(f.turn().delivered==N-1&&f.receipts==N-1);
        auto next=f.complete(1);
        for(unsigned i=0;i<128;++i) {
            assert(f.publication.pump(f.events,f.ticket,N-1)==Send::Queued);
            assert(f.turn().delivered==0&&!f.capture(next));
            assert(f.bus.notice(next)&&!f.buffers[1].available());
        }
        assert(!f.publication.release(f.ticket));f.bind(N-1);
        assert(f.publication.rebind(f.events,f.ticket,N-1,f.targets[N-1]));
        assert(f.publication.pump(f.events,f.ticket,N-1)==Send::Accepted);
        assert(f.turn().delivered==1&&f.publication.release(f.ticket));
        assert(f.capture(next));f.pump_all();assert(f.turn().delivered==N);
        assert(f.receipts==2*N&&f.publication.release(f.ticket));
    } else if(!std::strcmp(scenario,"retire")) {
        using Small=completion_fanout_experiment::Publication<typename F::Value,N,C,uint8_t>;
        Small p;typename Small::Target routes[N]{};uint8_t out=0;
        for(unsigned i=1;i<=255;++i){assert(p.reserve(routes,F::all,out)==Reserve::Accepted&&out==i);assert(p.abandon(out));}
        out=99;assert(p.reserve(routes,F::all,out)==Reserve::Exhausted&&out==99);
        assert(!p.publish(1,typename F::Value{})&&!p.abandon(255)&&!p.release(255));
    } else assert(false);
    f.report();
}
template<bool D,unsigned N> void capacity(unsigned c,const char* scenario,unsigned mask) {
    if(c==1)run<D,N,1>(scenario,mask);else {assert(c==2);run<D,N,2>(scenario,mask);}
}
template<bool D> void targets(unsigned n,unsigned c,const char* scenario,unsigned mask) {
    if(n==1)capacity<D,1>(c,scenario,mask);else if(n==2)capacity<D,2>(c,scenario,mask);
    else {assert(n==4);capacity<D,4>(c,scenario,mask);}
}
int main(int argc,char** argv) {
    assert(argc==6);unsigned n=std::atoi(argv[2]),c=std::atoi(argv[3]),mask=std::atoi(argv[5]);
    if(!std::strcmp(argv[1],"dedicated"))targets<true>(n,c,argv[4],mask);
    else {assert(!std::strcmp(argv[1],"pooled"));targets<false>(n,c,argv[4],mask);}
}
