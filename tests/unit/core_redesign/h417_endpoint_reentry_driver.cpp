#include "h417_endpoint.hpp"
#include <cassert>
#include <functional>
#include <iostream>
using namespace h417_transport_experiment;
struct Gate {bool held=false;bool try_enter(){if(held)return false;held=true;return true;}void leave(){held=false;}};
struct Hook:NoHook {
    static inline std::function<void(unsigned)> function;
    static void point(unsigned,unsigned point){if(function)function(point);}
};
struct Observe {
    unsigned active=0;
    void session(unsigned,bool,unsigned,unsigned a,unsigned,bool,unsigned,unsigned){active=a;}
    void start(unsigned,unsigned,unsigned,bool,unsigned,unsigned,unsigned,unsigned){}
    void message(const Message&){}void blocks(){}void block(unsigned,unsigned,unsigned,unsigned,uint32_t){}void end(){}
};
template<unsigned C> void check() {
    for(unsigned stage:{1u,3u,5u}) {
        Shared<Owners::Fixed,1> state;Gate gate;View<decltype(state),Gate> view(state,gate);Endpoint<C,decltype(view),Hook> endpoint;
        assert(endpoint.begin(view,1));Ticket ticket{};assert(view.push(0,1,10,ticket)==Result::Done && view.hint(ticket)==Result::Done);
        bool once=true;PumpResult nested{};
        Hook::function=[&](unsigned point){
            if(point!=stage || !once)return;
            once=false;
            nested=endpoint.opportunity(experiment::WorkOpportunity::Explicit,experiment::WorkProfile::M);
            Observe observed;assert(view.observe(observed)==Result::Done && observed.active==2);
            assert(endpoint.borrowed && endpoint.in_turn);
        };
        const auto outer=endpoint.opportunity(experiment::WorkOpportunity::Explicit,experiment::WorkProfile::M);
        Hook::function=nullptr;
        assert(!once && nested.context==experiment::TurnStatus::Reentrant && nested.transport==Result::Rejected);
        assert(outer.transport==Result::Done && !endpoint.borrowed && !endpoint.in_turn && endpoint.messages==1);
        Observe observed;assert(view.observe(observed)==Result::Done && observed.active==0);
    }
}
int main(){check<1>();check<2>();check<3>();check<4>();std::cout<<"{\"reentry_boundaries\":12}\n";}
