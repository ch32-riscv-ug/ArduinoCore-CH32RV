#include "../fixture.h"
#include <cstdlib>
#include <iostream>
#include <string>
extern "C" void vExperimentAssert(void) { std::cerr<<"kernel fixture assertion\n";std::abort(); }
static void driver(void *) {
    std::string command;
    while(std::cin>>command) {
        int accepted=-1;
        if(command=="input" || command=="complete") {
            uint32_t value;std::cin>>value;accepted=fixture_offer(command=="complete",value);
        } else if(command=="drain")fixture_drain();
        else if(command!="observe")std::abort();
        uint32_t output[16];fixture_snapshot(accepted,output);
        std::cout<<'[';
        for(unsigned i=0;i<16;++i) {
            if(i)std::cout<<',';
            if(!i)std::cout<<accepted;else std::cout<<output[i];
        }
        std::cout<<"]\n";
    }
    std::exit(0); // Process completion, not a kernel task shutdown protocol.
}
int main() { fixture_start(driver); }
