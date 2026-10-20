#include "../../unit/core_redesign/data_parts.hpp"

#ifndef PART
#define PART 0
#endif
#ifndef CAPACITY
#define CAPACITY 4
#endif
#ifndef WIDTH
#define WIDTH 4
#endif
#ifndef VARIANT
#define VARIANT 0
#endif
#ifndef USED
#define USED 1
#endif

volatile uint32_t operation, argument, observation;

#if PART == 1
#if WIDTH == 16
struct Payload { uint32_t data[4]; };
static Payload make(uint32_t value) { return {{value, value + 1, value + 2, value + 3}}; }
static uint32_t first(Payload value) {
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) result = result * 33u ^ value.data[i];
    return result;
}
#elif WIDTH == 1
using Payload = uint8_t;
static Payload make(uint32_t value) { return uint8_t(value); }
static uint32_t first(Payload value) { return value; }
#else
using Payload = uint32_t;
static Payload make(uint32_t value) { return value; }
static uint32_t first(Payload value) { return value; }
#endif
#if VARIANT == 1
Payload borrowed[CAPACITY];
experiment::QueueView<Payload> component(borrowed, CAPACITY);
#elif VARIANT == 2
bool ready;
experiment::Queue<Payload, CAPACITY, experiment::WakeFlag> component({&ready});
#else
experiment::Queue<Payload, CAPACITY> component;
#endif
#elif PART == 2
experiment::ByteRing<CAPACITY> component;
#elif PART == 3
experiment::MessageBuffer<CAPACITY, WIDTH> component;
#elif PART == 4
experiment::Semaphore<CAPACITY> component;
#elif PART == 5
experiment::EventBits component;
#elif PART == 6
experiment::TryLock component;
#elif PART == 7
experiment::Pool<WIDTH, CAPACITY> component;
#endif

extern "C" __attribute__((section(".text.entry"))) void data_entry() {
    for (;;) {
        const uint32_t op = operation, value = argument;
#if PART == 1 && USED
        Payload out{};
        observation = op ? component.push(make(value)) : component.pop(out);
        observation = observation + first(out) + sizeof(component);
#if VARIANT == 2
        observation += ready; ready = false;
#endif
#elif PART == 2 && USED
        uint8_t bytes[257]{};
        bytes[0] = uint8_t(value);
        observation = op ? component.write(bytes, value % (CAPACITY + 1)) : component.read(bytes, value % (CAPACITY + 1));
        observation += bytes[0] + sizeof(component);
#elif PART == 3 && USED
        uint8_t bytes[WIDTH + 1]{};
        bytes[0] = uint8_t(value); unsigned length = 0;
        observation = op ? component.push(bytes, value % (WIDTH + 2)) : component.pop(bytes, WIDTH, length);
        observation += length + bytes[0] + sizeof(component);
#elif PART == 4 && USED
        observation = op ? component.release() : component.acquire();
        observation += sizeof(component);
#elif PART == 5 && USED
        uint32_t out = 0;
        if (op == 1) component.set(value);
        else if (op == 2) component.clear(value);
        else observation = component.take(value, (op & 4) != 0, (op & 8) != 0, out);
        observation += out + sizeof(component);
#elif PART == 6 && USED
        observation = op ? component.acquire(value) : component.release(value);
        observation += sizeof(component);
#elif PART == 7 && USED
        typename experiment::Pool<WIDTH, CAPACITY>::Token token{};
        observation = component.allocate(token);
        if (auto data = component.data(token)) { data[0] = uint8_t(value); observation += data[0]; }
        observation += component.release(token) + sizeof(component);
#else
        observation = op + value;
#endif
    }
}
