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
#if CASE == 1 || CASE == 2
extern "C" {
volatile uint8_t initialized[3] = {0x5a, 0x12, 0xe7};
volatile uint8_t zeroed[5];
volatile uint32_t signature;
}
#endif
#if CASE == 2
extern "C" { volatile uint32_t order; }
void preinitialize() { order = order * 10 + 1; }
__attribute__((section(".preinit_array"),used)) void (*const preinitialize_entry)() = preinitialize;
__attribute__((constructor(101))) void early_initialize() { order = order * 10 + 2; }
struct Probe { Probe() { order = order * 10 + 3; } } probe;
#endif
#if CASE == 6 || CASE == 7
extern "C" { volatile unsigned requested_pin; }
#endif
#if CASE == 8
FixedPin<Mmio, Port, ClockMask, 0> pin;
#endif
#if CASE == 8
// Must disappear if unused; no registration or constructor with side effects.
#endif
extern "C" void setup();
extern "C" void loop();
extern "C" __attribute__((noreturn)) void experiment_main() {
#if CASE == 1 || CASE == 2
    uint32_t ok = initialized[0] == 0x5a && initialized[1] == 0x12 && initialized[2] == 0xe7;
    for (auto byte : zeroed) ok &= byte == 0;
#if CASE == 2
    ok &= order == 123;
#endif
    signature = ok ? 0x12345678u : 0xbad0bad0u;
#elif CASE == 6 || CASE == 7
#if CASE == 6
    DynamicPin<Mmio> dynamic{Port, ClockMask, requested_pin};
    dynamic.begin(); for (;;) { dynamic.write(true); dynamic.write(false); }
#else
    const unsigned pin_number = requested_pin;
    raw_begin<Mmio>(Port, ClockMask, pin_number);
    for (;;) { raw_write<Mmio>(Port, pin_number, true); raw_write<Mmio>(Port, pin_number, false); }
#endif
#elif CASE == 3 || CASE == 4 || CASE == 5
    setup(); for (;;) loop();
#endif
    for (;;) asm volatile("");
}
