#include "../../unit/core_redesign/owned_queue.hpp"
#include "../../unit/core_redesign/shared_channels.hpp"
#include "irq_gate.hpp"

volatile unsigned operation, argument, observation;
struct Payload {
    unsigned words[4]{};
};
unsigned digest(const Payload& p) {
    return p.words[0] ^ p.words[1] * 17u ^ p.words[2] * 257u ^ p.words[3] * 65537u;
}
struct Owner {
    Payload payload;
    explicit Owner(unsigned n = 0) noexcept : payload{{n, n + 1, n + 2, n + 3}} {}
    Owner(const Owner&) = delete;
    Owner(Owner&& other) noexcept : payload(other.payload) { other.payload = {}; }
    Owner& operator=(Owner&& other) noexcept { payload = other.payload; other.payload = {}; return *this; }
    // Observable external resource return, not a no-op trivial destructor.
    ~Owner() noexcept { observation ^= digest(payload); }
};
#if PART == 1
using Component = experiment::OwnedQueue<Owner, CAPACITY>;
alignas(Component) unsigned char component_storage[sizeof(Component)];
#elif PART == 2
using Gate = experiment::QingkeIrqGate<VARIANT == 0 ? 0x300 : 0x800>;
Gate gate;
using Component = experiment::GuardedChannel<Payload, CAPACITY, Gate>;
Component component(gate);
#elif PART == 3
using Component = experiment::SpscChannel<Payload, CAPACITY>;
Component component;
#endif

extern "C" __attribute__((section(".text.entry"))) void data_entry() {
#if PART == 1 && USED
    // Explicit construction: this entry assumes reset/startup was handled by
    // its caller. No global destructor registration or hidden initialization.
    auto* component = ::new (static_cast<void*>(component_storage)) Component;
#endif
    for (;;) {
#if PART == 1 && USED
        Owner value(argument);
        if (operation == 1) observation = component->push(std::move(value));
        else if (operation == 2) observation = component->pop(value) ? digest(value.payload) : 0;
        else component->clear();
#elif PART == 2 && USED
        const unsigned n = argument;
        Payload value{{n, n + 1, n + 2, n + 3}};
        if (operation == 1) observation = unsigned(component.push(value));
        else if (operation == 2) observation = component.pop(value) == experiment::ChannelResult::Ok ? digest(value) : 0;
        else if (operation == 3) observation = unsigned(component.prepare_wait());
        else { bool wake = false; observation = unsigned(component.take_wake(wake)) * 2 + wake; }
#elif PART == 3 && USED
        const unsigned n = argument;
        Payload value{{n, n + 1, n + 2, n + 3}};
        if (operation == 1) observation = component.push(value);
        else observation = component.pop(value) ? digest(value) : 0;
#else
        observation = argument ^ operation;
#endif
    }
}
