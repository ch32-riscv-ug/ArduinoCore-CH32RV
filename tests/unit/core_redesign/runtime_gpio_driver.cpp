#include "../../platform/core_redesign/runtime/min_gpio.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
using namespace runtime_experiment;
struct Fake {
    struct Event { unsigned operation; uintptr_t address; uint32_t value; };
    static inline std::vector<Event> events;
    static inline uintptr_t port;
    static inline uint32_t rcc, config;
    static uint32_t read(uintptr_t p) {
        assert(p == ClockEnable || p == port);
        const auto v = p == ClockEnable ? rcc : config;
        events.push_back({0,p,v}); return v;
    }
    static void write(uintptr_t p, uint32_t v) {
        assert(p == ClockEnable || p == port || p == port+0x10);
        if (p == ClockEnable) rcc=v;
        if (p == port) config=v;
        events.push_back({1,p,v});
    }
};
template<bool X> void run(unsigned variant, unsigned number, uint32_t rcc, uint32_t config, bool high) {
    constexpr uintptr_t port=X ? 0x40010800 : 0x40011400;
    constexpr uint32_t mask=X ? 4 : 32;
    Fake::events.clear();Fake::port=port;Fake::rcc=rcc;Fake::config=config;
    bool a,b;
    if (variant==0) { assert(number==0);a=raw_begin<Fake>(port,mask,0);b=raw_write<Fake>(port,0,high); }
    else if (variant==1) { assert(number==0);FixedPin<Fake,port,mask,0> pin;a=pin.begin();b=pin.write(high); }
    else if (variant==2) { a=raw_begin<Fake>(port,mask,number);b=raw_write<Fake>(port,number,high); }
    else { DynamicPin<Fake> pin{port,mask,number};a=pin.begin();b=pin.write(high); }
    std::printf("%u %u %u %u",unsigned(a),unsigned(b),Fake::rcc,Fake::config);
    for (auto e:Fake::events)std::printf(" %u %lu %u",e.operation,static_cast<unsigned long>(e.address),e.value);
    std::puts("");
}
int main() {
    unsigned v,x,p,r,c,h;
    while (std::scanf("%u %u %u %u %u %u",&v,&x,&p,&r,&c,&h)==6) {
        assert(v<4 && x<2);if(x)run<true>(v,p,r,c,h);else run<false>(v,p,r,c,h);
    }
}
