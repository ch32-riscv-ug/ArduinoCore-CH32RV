#include "completion_arbiter.hpp"
#include "completion_delivery.hpp"
#include "completion_fanout.hpp"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace experiment;
using completion_fanout_experiment::Reserve;
using completion_fanout_experiment::Send;
// Application composition comparison, not another public event policy. Values
// are completion metadata, not PPS commands or display pixels.
struct OptionalValue {uint32_t sequence=0,digest=0,bytes=0;};
struct Application {
    using Bus=completion_arbiter_experiment::Arbiter<1,1,true>;
    using Value=OptionalValue;
    using Pub=completion_fanout_experiment::Publication<Value,2,1>;
    using Events=Pub::Events;
    Bus bus;Bus::Buffer buffer;Bus::Token source{};
    Pub publication;Events events;Pub::Target targets[2]{};
    struct Context {Application* self=nullptr;unsigned index=0;} contexts[2];
    Pub::Ticket ticket=0;
    unsigned policy=0,source_sequence=0;
    uint32_t produced=0,taken=0,offered=0,dropped=0,removed=0,overwritten=0;
    bool pending=false,attached[2]{true,true},dirty=false;
    Value latest{};
    std::vector<unsigned> control,display;
    explicit Application(unsigned p):policy(p) {
        assert(bus.open(0));
        for(unsigned i=0;i<2;++i){contexts[i]={this,i};bind(i);}
        events.initialize();
    }
    void bind(unsigned i) {
        targets[i]=events.add(handler,&contexts[i],i?WorkDomain::User:WorkDomain::System,false);
        assert(events.valid(targets[i]));
    }
    static void validate(const Value& value) {
        uint32_t hash=2166136261u;
        for(unsigned i=0;i<4;++i)hash=(hash^uint8_t(value.sequence+i))*16777619u;
        assert(value.sequence&&value.bytes==4&&value.digest==hash);
    }
    static void handler(void* raw,const Events::Delivery& delivery) {
        auto& c=*static_cast<Context*>(raw);auto& a=*c.self;
        if(!delivery.has_message)return;
        Value value{};
        if(c.index==1&&a.policy==1&&delivery.message.generation==0) {
            value=delivery.message.value;if(value.sequence){validate(value);a.display.push_back(value.sequence);}
        } else if(a.publication.receive(delivery,value)) {
            validate(value);
            if(c.index)a.display.push_back(value.sequence);else a.control.push_back(value.sequence);
        }
    }
    void release_done() {
        if(publication.phase()==2&&publication.delivered()==publication.required())assert(publication.release(ticket));
    }
    unsigned produce(unsigned sequence) {
        if(!sequence||sequence>65535)return 2;
        const auto result=bus.submit(0,&buffer,4,IoMode::Read,42,source);
        if(result!=IoStart::Accepted)return 1;
        Bus::Token dispatched{};assert(bus.dispatch(dispatched));
        assert(dispatched.generation==source.generation);
        assert(bus.finish(source,IoResult::Success,4,uint8_t(sequence),true));
        source_sequence=sequence;pending=true;++produced;return 0;
    }
    void poll() {
        if(pending&&publication.phase()==0) {
            assert(publication.reserve(targets,policy==0?3:1,ticket)==Reserve::Accepted);
            Bus::Completion completion{};assert(completion_delivery_experiment::collect(bus,source,completion));
            const Value value{source_sequence,completion.digest,completion.transferred};
            assert(publication.publish(ticket,value));pending=false;++taken;
            if(policy==1) {
                ++offered;
                if(events.send(targets[1],Pub::Payload{0,1,value})!=EventSend::Accepted)++dropped;
            } else if(policy==2) {
                if(dirty)++overwritten;
                latest=value;dirty=true;
            }
        }
        if(publication.phase()==2)for(unsigned i=0;i<(policy==0?2u:1u);++i) {
            const auto result=publication.pump(events,ticket,i);
            assert(result==Send::Accepted||result==Send::Full||result==Send::Stale||result==Send::Queued||result==Send::Delivered);
        }
        // Delay/S permits the System control handler, not the User display.
        events.turn(2,WorkOpportunity::Delay,WorkProfile::S);
        release_done();
    }
    void draw() {
        events.turn(2,WorkOpportunity::Explicit,WorkProfile::U);
        if(policy==2&&attached[1]&&dirty){validate(latest);display.push_back(latest.sequence);dirty=false;}
        release_done();
    }
    bool detach(unsigned i) {
        if(i>1||!attached[i])return false;
        if(i==1&&policy==1)removed+=events.queued(targets[i]);
        assert(events.remove(targets[i]));attached[i]=false;return true;
    }
    bool attach(unsigned i) {
        if(i>1||attached[i])return false;
        bind(i);attached[i]=true;
        if(publication.phase()==2&&(publication.required()&(1u<<i))&&!(publication.delivered()&(1u<<i)))
            assert(publication.rebind(events,ticket,i,targets[i]));
        return true;
    }
    unsigned fill(unsigned i) {
        if(i>1)return 2;
        const auto result=events.send(targets[i],Pub::Payload{});
        return result==EventSend::Accepted?0:result==EventSend::Full?1:2;
    }
    static void array(const std::vector<unsigned>& values) {
        std::putchar('[');for(unsigned i=0;i<values.size();++i)std::printf("%s%u",i?",":"",values[i]);std::putchar(']');
    }
    void report(unsigned result) {
        std::printf("{\"result\":%u,\"produced\":%u,\"taken\":%u,\"pending\":%s,\"available\":%s,\"phase\":%u,\"required\":%u,\"queued\":%u,\"delivered\":%u,\"mailboxes\":[%u,%u],\"attached\":[%s,%s],\"offered\":%u,\"dropped\":%u,\"removed\":%u,\"overwritten\":%u,\"dirty\":%s,\"latest\":%u,\"control\":",
            result,produced,taken,pending?"true":"false",buffer.available()?"true":"false",publication.phase(),publication.required(),publication.queued(),publication.delivered(),
            events.queued(targets[0]),events.queued(targets[1]),attached[0]?"true":"false",attached[1]?"true":"false",offered,dropped,removed,overwritten,dirty?"true":"false",latest.sequence);
        array(control);std::printf(",\"display\":");array(display);std::puts("}");
    }
};
int main(int argc,char** argv) {
    assert(argc==2);unsigned policy;
    if(!std::strcmp(argv[1],"all"))policy=0;
    else if(!std::strcmp(argv[1],"once"))policy=1;
    else {assert(!std::strcmp(argv[1],"latest"));policy=2;}
    Application app(policy);std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream in(line);std::string command;in>>command;unsigned result=0,value=0;
        if(command=="produce"){in>>value;result=app.produce(value);}
        else if(command=="poll")app.poll();
        else if(command=="draw")app.draw();
        else if(command=="detach"){in>>value;result=app.detach(value);}
        else if(command=="attach"){in>>value;result=app.attach(value);}
        else if(command=="fill"){in>>value;result=app.fill(value);}
        else assert(command=="snapshot");
        app.report(result);
    }
}
