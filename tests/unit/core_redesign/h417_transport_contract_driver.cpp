#include "h417_endpoint.hpp"
#include <cassert>
#include <iostream>
using namespace h417_transport_experiment;
struct Gate {
    bool blocked=false,held=false;unsigned attempts=0;
    bool try_enter(){++attempts;if(blocked || held)return false;held=true;return true;}
    void leave(){assert(held);held=false;}
};
struct Observe {
    unsigned active=0,hints=0,epoch=0;
    void session(unsigned e,bool,unsigned,unsigned a,unsigned,bool,unsigned h0,unsigned h1){epoch=e;active=a;hints=h0|h1;}
    void start(unsigned,unsigned,unsigned,bool,unsigned,unsigned,unsigned,unsigned){}
    void message(const Message&){}void blocks(){}void block(unsigned,unsigned,unsigned,unsigned,uint32_t){}void end(){}
};
void contracts() {
    using State=Shared<Owners::Fixed,3>;State shared;Gate gate;View<State,Gate> source(shared,gate),consumer(shared,gate);
    Token token{99,99,99,99};Ticket ticket{99,99,99};TurnInput input{{99,99,99},99};
    gate.blocked=true;
    assert(source.push(0,1,10,ticket)==Result::Busy && ticket.epoch==99);
    assert(source.reserve(0,token)==Result::Busy && token.generation==99);
    assert(consumer.open_turn(1,input)==Result::Busy && input.hints==99 && input.permit.serial==99);
    gate.blocked=false;
    assert(source.hint({2,1,1})==Result::Stale && source.hint({1,4,1})==Result::Rejected);
    assert(consumer.open_turn(0,input)==Result::Rejected && input.hints==99);
    for(unsigned i=0;i<4;++i)assert(source.push(0,1,10+i,ticket)==Result::Done);
    const Ticket prior=ticket;assert(source.push(0,1,99,ticket)==Result::Full && ticket.epoch==prior.epoch && ticket.destination==prior.destination);
    assert(source.hint(ticket)==Result::Done && consumer.open_turn(1,input)==Result::Done);
    TurnInput untouched{{99,99,99},99};assert(consumer.open_turn(1,untouched)==Result::Busy && untouched.hints==99);
    Message message{{99,99,99,99}};
    gate.blocked=true;assert(consumer.pop(input.permit,message)==Result::Busy && message.words[0]==99);
    gate.blocked=false;
    for(unsigned i=0;i<4;++i){assert(consumer.pop(input.permit,message)==Result::Done);for(unsigned j=0;j<4;++j)assert(message.words[j]==10+i+j);}
    const Message old=message;assert(consumer.pop(input.permit,message)==Result::Empty);
    for(unsigned i=0;i<4;++i)assert(message.words[i]==old.words[i]);
    Observe observed;assert(source.observe(observed)==Result::Done && !observed.hints && observed.active==2);
    gate.blocked=true;assert(consumer.close_turn(input.permit)==Result::Busy);
    gate.blocked=false;assert(consumer.close_turn(input.permit)==Result::Done);
    const Permit stale=input.permit;assert(consumer.open_turn(1,input)==Result::Done);
    assert(consumer.close_turn(stale)==Result::Stale && consumer.close_turn(input.permit)==Result::Done);
    assert(source.reserve(0,token)==Result::Done);
    const Token lease=token;
    assert(source.write(0,token,0,77)==Result::Done && source.publish(0,token,1,ticket)==Result::Rejected);
    for(unsigned i=1;i<64;++i)assert(source.write(0,token,i,i)==Result::Done);
    assert(source.publish(0,token,1,ticket)==Result::Done && source.hint(ticket)==Result::Done);
    Token second{};assert(source.reserve(0,second)==Result::Done);
    Token sentinel{99,99,99,99};assert(source.reserve(0,sentinel)==Result::Rejected && sentinel.generation==99);
    assert(consumer.open_turn(1,input)==Result::Done && consumer.take(input.permit,token)==Result::Done);
    unsigned output=999;assert(consumer.read(input.permit,token,64,output)==Result::Rejected && output==999);
    Token wrong=token;++wrong.generation;
    assert(consumer.read(input.permit,wrong,0,output)==Result::Rejected && output==999);
    assert(consumer.release(input.permit,wrong)==Result::Rejected);
    wrong=token;++wrong.epoch;assert(consumer.read(input.permit,wrong,0,output)==Result::Stale && output==999);
    assert(consumer.read(input.permit,token,0,output)==Result::Done && output==77);
    assert(source.recover()==Result::Done);
    assert(consumer.read(input.permit,token,0,output)==Result::Closed && output==77);
    Ticket saved=ticket;assert(source.push(0,1,99,ticket)==Result::Closed && ticket.epoch==saved.epoch);
    assert(source.enable(0,false)==Result::Done && source.enable(1,false)==Result::Done && source.dma(true)==Result::Done);
    assert(source.attest(7)==Result::Busy && source.restart()==Result::Busy);
    assert(consumer.close_turn(input.permit)==Result::Done);
    assert(source.attest(7)==Result::Done && source.restart()==Result::Done);
    assert(source.hint(saved)==Result::Stale && source.write(0,lease,1,99)==Result::Stale);
    assert(consumer.open_turn(1,input)==Result::Closed);
    assert(source.enable(1,true)==Result::Done && consumer.open_turn(1,input)==Result::Done);
    assert(consumer.release(input.permit,lease)==Result::Stale && consumer.close_turn(input.permit)==Result::Done);
    Shared<Owners::Fixed,0> empty;Gate no_gate;View<decltype(empty),Gate> disabled(empty,no_gate);
    assert(disabled.push(0,1,99,ticket)==Result::Unsupported && disabled.reserve(0,token)==Result::Unsupported);
    assert(disabled.write(0,token,0,99)==Result::Unsupported && disabled.publish(0,token,1,ticket)==Result::Unsupported);
    assert(disabled.pop({},message)==Result::Unsupported && disabled.take({},token)==Result::Unsupported);
    assert(disabled.read({},token,0,output)==Result::Unsupported && disabled.release({},token)==Result::Unsupported && no_gate.attempts==0);
}
template<unsigned C> void policy() {
    using State=Shared<Owners::Fixed,1>;using Transport=View<State,Gate>;using Ep=Endpoint<C,Transport>;
    static_assert(!std::is_copy_constructible<Ep>::value && !std::is_move_constructible<Ep>::value,"local endpoint identity is fixed");
    for(unsigned where=1;where<=2;++where)for(unsigned profile=0;profile<3;++profile)for(unsigned user=0;user<3;++user) {
        State state;Gate gate;Transport transport(state,gate);Ep endpoint;
        assert(endpoint.begin(transport,1,user?WorkDomain::User:WorkDomain::System,user==2));
        assert(!endpoint.begin(transport,1));Ticket ticket{};
        assert(transport.push(0,1,10,ticket)==Result::Done && transport.hint(ticket)==Result::Done);
        const auto result=endpoint.opportunity(WorkOpportunity(where),WorkProfile(profile));
        const bool allowed=profile!=0 && (user==0 || (profile==2 && user==2));
        assert(endpoint.messages==unsigned(allowed));assert(result.context==(profile==0?TurnStatus::Suppressed:TurnStatus::Ran));
        endpoint.opportunity(WorkOpportunity::Explicit,WorkProfile::M);assert(endpoint.messages==1 && !endpoint.borrowed);
    }
    for(unsigned context=1;context<4;++context) {
        State state;Gate gate;Transport transport(state,gate);Ep endpoint;assert(endpoint.begin(transport,1));Ticket ticket{};
        assert(transport.push(0,1,10,ticket)==Result::Done && transport.hint(ticket)==Result::Done);
        const auto result=endpoint.opportunity(WorkOpportunity::Explicit,WorkProfile::M,WorkContext(context));
        assert(result.context==(context==2?TurnStatus::Reentrant:TurnStatus::Forbidden));assert(!endpoint.messages && !endpoint.borrowed);
        endpoint.opportunity(WorkOpportunity::Explicit,WorkProfile::M);assert(endpoint.messages==1);
    }
}
int main(){contracts();policy<1>();policy<2>();policy<3>();policy<4>();std::cout<<"{\"transport_sequence\":1,\"policy_cases\":72,\"context_cases\":12}\n";}
