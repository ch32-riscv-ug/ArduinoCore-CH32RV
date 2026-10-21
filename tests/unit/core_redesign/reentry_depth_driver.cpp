#include "reentry_depth.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
using namespace experiment;
template<unsigned Limit,bool Nested> void rig() {
    ReentryDepth<Limit,Nested> app;
    std::string command;
    while (std::cin>>command) {
        int result=-1;
        if (command=="node") {
            unsigned i,kind,opt;int dependency,needs,holds;
            std::cin>>i>>dependency>>needs>>holds>>kind>>opt;assert(i<5);
            app.nodes[i]={int8_t(dependency),int8_t(needs),int8_t(holds),WaitProducer(kind),opt!=0};
        } else if (command=="owner") { unsigned r;int actor;std::cin>>r>>actor;assert(r<2);app.owner[r]=int8_t(actor); }
        else if (command=="permission") {
            unsigned where,profile,opt,irq,remote;std::cin>>where>>profile>>opt>>irq>>remote;
            app.permission={WorkOpportunity(where),WorkProfile(profile),opt!=0,irq!=0,remote!=0,true};
        } else if (command=="run") { unsigned i,context;std::cin>>i>>context;result=int(app.run(i,WorkContext(context))); }
        else if (command=="external") { unsigned i;std::cin>>i;result=app.external(i); }
        else if (command=="round") { for(unsigned i=0;i<5;++i)result=int(app.run(i)); }
        else { assert(command=="observe"); }
        assert(app.active==0 && app.depth==0);
        std::cout<<"["<<result<<','<<unsigned(app.done)<<','<<unsigned(app.peak)<<','<<app.calls<<','
                 <<app.limited<<','<<app.active_hits<<','<<app.held_hits<<','<<int(app.owner[0])<<','<<int(app.owner[1])<<"]\n";
    }
}
int main(int argc,char** argv) {
    assert(argc==3);unsigned d=unsigned(std::strtoul(argv[1],nullptr,10));bool nested=std::strtoul(argv[2],nullptr,10)!=0;
    if (!nested) { assert(d==1);rig<1,false>(); }
    else if(d==1)rig<1,true>();else if(d==2)rig<2,true>();else{assert(d==4);rig<4,true>();}
}
