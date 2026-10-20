#pragma once
#include <stdint.h>
namespace runtime_experiment {
// Only pins 0..7 and output mode 01/CNF=00 are compared. No IRQ, clock
// frequency selection, pin ownership or complete product GPIO API is implied.
constexpr uintptr_t ClockEnable = 0x40021018u;
template<class Bus> bool raw_begin(uintptr_t port, uint32_t clock_mask, unsigned pin) {
    if (pin >= 8) return false;
    Bus::write(ClockEnable, Bus::read(ClockEnable) | clock_mask);
    Bus::write(port + 0x10, 1u << (pin + 16));
    const unsigned shift = 4 * pin;
    Bus::write(port, (Bus::read(port) & ~(15u << shift)) | (1u << shift));
    return true;
}
template<class Bus> bool raw_write(uintptr_t port, unsigned pin, bool high) {
    if (pin >= 8) return false;
    Bus::write(port + 0x10, 1u << (pin + (high ? 0 : 16))); return true;
}
template<class Bus, uintptr_t Port, uint32_t ClockMask, unsigned Pin> struct FixedPin {
    bool begin() { return raw_begin<Bus>(Port, ClockMask, Pin); }
    bool write(bool high) { return raw_write<Bus>(Port, Pin, high); }
};
template<class Bus> struct DynamicPin {
    uintptr_t port;
    uint32_t clock_mask;
    unsigned pin;
    bool begin() { return raw_begin<Bus>(port, clock_mask, pin); }
    bool write(bool high) { return raw_write<Bus>(port, pin, high); }
};
} // namespace runtime_experiment
