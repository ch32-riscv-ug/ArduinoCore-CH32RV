#include "../../unit/core_redesign/event_executor.hpp"
#include "../../unit/core_redesign/io_lifecycle.hpp"
#include "../../unit/core_redesign/deadline_timer.hpp"

volatile uint32_t operation, argument[4], now, observation;
#if PART != 0
struct Payload { uint32_t words[4]{}; };
using Component = experiment::EventExecutor<Payload, CAPACITY, WIDTH>;
Component component;
Component::Token tokens[CAPACITY];
#if PART == 2
experiment::IoSlot<32> io;
experiment::IoBuffer<32> buffer;
experiment::DeadlineTimer timer;
#endif
void handler(void*, const Component::Delivery& d) {
    if (d.has_message)
        observation ^= d.message.words[0] ^ d.message.words[1] * 17u ^
                       d.message.words[2] * 257u ^ d.message.words[3] * 65537u;
    observation ^= d.notifications;
    if (argument[0] & 0x100) component.notify(d.target, 4);
    if (argument[0] & 0x200) component.send(tokens[(d.target.index + 1) % CAPACITY], d.message);
#if PART == 2
    if (d.notifications & 1) {
        experiment::IoSlot<32>::Completion c;
        if (io.take(io.token(), c)) observation ^= c.digest ^ c.transferred ^ unsigned(c.result);
    }
    if (d.notifications & 2) {
        experiment::DeadlineTimer::Expiration expiry;
        if (timer.take(expiry)) observation ^= expiry.latest_due ^ expiry.skipped ^ uint32_t(expiry.saturated);
    }
#endif
}
#endif
extern "C" __attribute__((section(".text.entry"))) void data_entry() {
#if PART != 0 && USED
    for (unsigned i = 0; i < CAPACITY; ++i)
        tokens[i] = component.add(handler, nullptr, i % 2 ? experiment::WorkDomain::User : experiment::WorkDomain::System, i % 3 != 0);
    component.initialize();
#if PART == 2
    io.open();
#endif
#endif
    for (;;) {
#if PART != 0 && USED
        const unsigned index = argument[0] % CAPACITY;
        const auto n = argument[1];
        switch (operation % (PART == 2 ? 7 : 3)) {
        case 0: observation = unsigned(component.send(tokens[index], Payload{{n, n + 1, n + 2, n + 3}})); break;
        case 1: observation = component.notify(tokens[index], n); break;
        case 2: {
            constexpr auto opportunity = VARIANT == 0 || VARIANT >= 4 ? experiment::WorkOpportunity::Explicit : experiment::WorkOpportunity::Delay;
            constexpr auto profile = VARIANT == 1 ? experiment::WorkProfile::S : VARIANT == 2 ? experiment::WorkProfile::U : experiment::WorkProfile::M;
            constexpr unsigned quota = VARIANT == 4 ? 1 : VARIANT == 5 ? 8 : 4;
            const auto r = component.turn(quota, opportunity, profile);
            observation = r.selected * 256 + r.delivered; break;
        }
#if PART == 2
        case 3: observation = unsigned(io.start(&buffer, 32, experiment::IoMode::Read, 64)); break;
        case 4: observation = io.hardware_terminal(io.token(), experiment::IoResult(n), argument[2], uint8_t(argument[3])); break;
        case 5: {
            experiment::IoSlot<32>::Token request;
            if (io.peek_notice(request) && component.notify(tokens[0], 1)) io.ack_notice(request);
            timer.update(now);
            if (timer.ready()) component.notify(tokens[0], 2);
            break;
        }
        default: observation = timer.arm(now, n, argument[2]); break;
#endif
        }
#else
        observation = argument[0] ^ operation;
#endif
    }
}
