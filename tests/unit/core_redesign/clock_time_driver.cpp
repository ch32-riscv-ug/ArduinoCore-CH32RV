#include "../../platform/core_redesign/runtime/clock_time.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <type_traits>
using namespace runtime_experiment;
struct Fake {
    struct Event {unsigned type;uintptr_t address;uint32_t value;};
    static inline std::vector<Event> events;
    static inline uint32_t ctl=1,cfg=0,flash=0,trim=255,tickctl=0,sr=0,comparelo=0,comparehi=0;
    static inline uint64_t ticks=0,jump=0;
    static inline bool wide=false;
    static void advance(){ticks+=jump;if(!wide)ticks=uint32_t(ticks);}
    static uint32_t read(uintptr_t p) {
        uint32_t v;
        if(p==RccControl)v=ctl;else if(p==RccConfig)v=cfg;else if(p==FlashAccess)v=flash;
        else if(p==TickControl)v=tickctl;else if(p==TickLow){v=uint32_t(ticks);advance();}
        else {assert(p==TickHigh && wide);v=uint32_t(ticks>>32);advance();}
        events.push_back({0,p,v});return v;
    }
    static uint32_t read_byte(uintptr_t p){assert(p==PllTrim);events.push_back({2,p,trim});return trim;}
    static void write(uintptr_t p,uint32_t v) {
        events.push_back({1,p,v});
        if(p==RccControl)ctl=v;else if(p==RccConfig)cfg=v;else if(p==FlashAccess)flash=v;
        else if(p==TickControl)tickctl=v;else if(p==TickStatus)sr=v;
        else if(p==TickLow)ticks=(ticks&0xffffffff00000000ull)|v;
        else if(p==TickHigh){assert(wide);ticks=(uint64_t(v)<<32)|uint32_t(ticks);}
        else if(p==TickCompareLow)comparelo=v;else {assert(p==TickCompareHigh && wide);comparehi=v;}
    }
};
template<bool X> void run() {
    Fake::wide=X;Clock48<Fake,X> clock;Counter<Fake,X> counter;
    static_assert(!std::is_copy_constructible<decltype(clock)>::value && !std::is_move_constructible<decltype(clock)>::value,"state not copied");
    static_assert(!std::is_copy_constructible<decltype(counter)>::value && !std::is_move_constructible<decltype(counter)>::value,"counter not copied");
    std::string op;uint64_t a,b,c,d;uint32_t low=0x12345678;uint64_t sample=0x1122334455667788ull;
    while(std::cin>>op) {
        Fake::events.clear();unsigned status=99;
        if(op=="clock_begin"){std::cin>>a;status=unsigned(clock.begin(a!=0));}
        else if(op=="clock_step")status=unsigned(clock.step());
        else if(op=="clock_abandon")status=clock.abandon();
        else if(op=="hardware"){std::cin>>a>>b>>c>>d;Fake::ctl=a;Fake::cfg=b;Fake::flash=c;Fake::trim=uint8_t(d);}
        else if(op=="counter_begin"){std::cin>>a>>b>>c;status=unsigned(counter.begin(uint32_t(a),b!=0,c!=0));}
        else if(op=="stop")status=counter.stop();
        else if(op=="ticks"){std::cin>>a>>b;Fake::ticks=X?a:uint32_t(a);Fake::jump=b;}
        else if(op=="counter_control"){std::cin>>a;Fake::tickctl=a;}
        else if(op=="low"){std::cin>>a;status=unsigned(counter.low(uint32_t(a),low));}
        else if(op=="sample"){std::cin>>a>>b;status=unsigned(counter.sample(uint32_t(a),sample,unsigned(b)));}
        else if(op=="convert_constant"){std::cin>>a;sample=elapsed_microseconds(uint32_t(a),6000000);status=0;}
        else if(op=="convert_specialized"){std::cin>>a;sample=uint32_t(a)/6u;status=0;}
        else {assert(op=="convert");std::cin>>a>>b;if(!b)status=1;else{sample=elapsed_microseconds(uint32_t(a),uint32_t(b));status=0;}}
        std::cout<<status<<' '<<unsigned(clock.stage)<<' '<<counter.epoch()<<' '<<counter.frequency()<<' '<<low<<' '<<sample<<' '
                 <<Fake::ctl<<' '<<Fake::cfg<<' '<<Fake::flash<<' '<<Fake::tickctl<<' '<<Fake::sr<<' '<<Fake::ticks<<' '<<Fake::comparelo<<' '<<Fake::comparehi;
        for(auto e:Fake::events)std::cout<<' '<<e.type<<' '<<e.address<<' '<<e.value;
        std::cout<<'\n';
    }
}
int main(int argc,char**argv){assert(argc==2);if(std::string(argv[1])=="X")run<true>();else{assert(std::string(argv[1])=="V");run<false>();}}
