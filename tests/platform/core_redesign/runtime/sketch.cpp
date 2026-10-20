#include "min_gpio.hpp"
using namespace runtime_experiment;
#if TARGET_X035
constexpr uintptr_t Port = 0x40010800;
constexpr uint32_t ClockMask = 1u << 2;
#else
constexpr uintptr_t Port = 0x40011400;
constexpr uint32_t ClockMask = 1u << 5;
#endif
struct Mmio {
    static uint32_t read(uintptr_t p) { return *reinterpret_cast<volatile uint32_t*>(p); }
    static void write(uintptr_t p, uint32_t value) { *reinterpret_cast<volatile uint32_t*>(p) = value; }
};
#if CASE == 4
FixedPin<Mmio, Port, ClockMask, 0> pin;
#endif
extern "C" void setup() {
#if CASE == 3
    raw_begin<Mmio>(Port, ClockMask, 0);
#elif CASE == 4
    pin.begin();
#endif
}
extern "C" void loop() {
#if CASE == 3
    raw_write<Mmio>(Port, 0, true); raw_write<Mmio>(Port, 0, false);
#elif CASE == 4
    pin.write(true); pin.write(false);
#endif
}
