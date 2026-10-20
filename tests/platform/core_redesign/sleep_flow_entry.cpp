#include "../../unit/core_redesign/sleep_coordinator.hpp"
using namespace sleep_experiment;
#if CASE == 2
using List = Registry<2>;
#elif CASE == 3
using List = Registry<4>;
#else
using List = Explicit<Port<>, Port<>>;
#endif
#if CASE > 0
Port<> ports_probe[2];
Clock clock_probe;
Timer timer_probe;
List list_probe;
Coordinator<List> coordinator_probe;
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation, argument[10], observed[20];
time_experiment::Stamp stamp_probe{};
time_experiment::Notice notice_probe{};
experiment::IoSlot<32>::Completion completion_probe{};
#if CASE == 2 || CASE == 3
List::Token registration_probe{};
#else
Registry<2>::Token registration_probe{};
#endif
uint32_t request_probe;
uint8_t byte_probe;
time_experiment::Result clock_status = time_experiment::Result::Closed;
#endif
extern "C" __attribute__((section(".text.entry"), noreturn)) void probe_main() {
    for (;;) {
#if CASE > 0 && !UNUSED
        const unsigned i = argument[0] & 1;
        const auto source = time_experiment::Sample{argument[2], argument[1], 6000000};
        const auto current = clock_probe.phase() == time_experiment::State::Paused ? time_experiment::Result::Paused : clock_status;
        switch (operation) {
        case 0:
#if CASE != 5
            observed[0] = ports_probe[0].init();
            observed[0] = ports_probe[1].init();
#endif
#if CASE != 4
            clock_status = clock_probe.begin({0, 1, 6000000}, 0, stamp_probe);
#endif
#if CASE >= 1 && CASE <= 3
#if CASE == 1
            observed[0] = list_probe.bind(ports_probe[0], ports_probe[1]);
#else
            observed[0] = list_probe.add(&ports_probe[0], &Port<>::callback, registration_probe);
            observed[0] = list_probe.add(&ports_probe[1], &Port<>::callback, registration_probe);
#endif
            observed[0] = coordinator_probe.bind(list_probe, clock_probe, timer_probe);
#endif
            break;
#if CASE != 5
        case 1: observed[0] = unsigned(ports_probe[i].start(argument[3])); break;
        case 2: observed[0] = ports_probe[i].finish(argument[1], experiment::IoResult(argument[2]), argument[3], uint8_t(argument[4]), argument[5] != 0); break;
        case 3: observed[0] = ports_probe[i].io.take(argument[1], completion_probe); break;
        case 4: observed[0] = ports_probe[i].buffer.read(argument[3], byte_probe); break;
        case 5: ports_probe[i].restoration = Reply(argument[3] % 3); break;
#endif
#if CASE != 4
        case 6: clock_status = clock_probe.observe(source, {argument[3], true}, stamp_probe); break;
        case 7: observed[0] = timer_probe.arm(current, stamp_probe, argument[3], argument[4]); break;
        case 8: observed[0] = unsigned(timer_probe.poll(current, stamp_probe)); break;
        case 9: observed[0] = timer_probe.take(notice_probe); break;
#endif
#if CASE >= 1 && CASE <= 3
        case 10: observed[0] = coordinator_probe.request(request_probe); break;
        case 11: observed[0] = unsigned(coordinator_probe.poll()); break;
        case 12: coordinator_probe.post_input(); break;
        case 13: observed[0] = coordinator_probe.take_input(); break;
        case 14: observed[0] = coordinator_probe.abort(); break;
        case 15: observed[0] = unsigned(coordinator_probe.enter(argument[6], argument[4] != 0, argument[5] != 0, Injection(argument[3] % 4))); break;
        case 16: observed[0] = coordinator_probe.wake(argument[6]); break;
        case 17: clock_status = coordinator_probe.restore_clock(argument[6], source, argument[3] != 0,
            uint64_t(argument[4]) | (uint64_t(argument[5]) << 32), argument[7], stamp_probe); break;
        case 18: observed[0] = coordinator_probe.retry(); break;
#if CASE == 2 || CASE == 3
        case 19: observed[0] = list_probe.remove({argument[1], uint8_t(argument[2])}); break;
        case 20: observed[0] = list_probe.add(&ports_probe[i], &Port<>::callback, registration_probe); break;
#endif
#elif CASE == 5
        case 10: observed[0] = clock_probe.pause(); break;
        case 11: clock_status = clock_probe.resume_unknown(source, argument[7], stamp_probe); break;
        case 12: {
            time_experiment::Dropped drop{};
            clock_status = clock_probe.resume_known(source, argument[4], false, stamp_probe, drop);
            break;
        }
#endif
        }
        observed[1] = unsigned(clock_status);
#if CASE != 5
        observed[2] = ports_probe[i].io.active() | (unsigned(ports_probe[i].io.terminal()) << 8) | (unsigned(ports_probe[i].buffer.available()) << 16);
        observed[3] = ports_probe[i].io.token();
        observed[4] = unsigned(completion_probe.result);
        observed[5] = completion_probe.transferred;
        observed[6] = completion_probe.digest;
        observed[7] = byte_probe;
#endif
#if CASE != 4
        observed[8] = uint32_t(stamp_probe.us);
        observed[9] = uint32_t(stamp_probe.us >> 32);
        observed[10] = stamp_probe.epoch;
        observed[11] = timer_probe.armed() | (unsigned(timer_probe.pending()) << 8);
        observed[12] = notice_probe.expiration.latest_due;
        observed[13] = notice_probe.expiration.skipped;
        observed[14] = notice_probe.epoch;
#endif
#if CASE >= 1 && CASE <= 3
        observed[15] = unsigned(coordinator_probe.phase());
        observed[16] = coordinator_probe.attempt();
        observed[17] = coordinator_probe.input_pending() | (unsigned(coordinator_probe.clock_ready()) << 8);
        observed[18] = list_probe.count() | (unsigned(list_probe.frozen()) << 8);
#if CASE > 1
        observed[19] = registration_probe.generation | (unsigned(registration_probe.index) << 24);
#endif
#endif
#else
        asm volatile("");
#endif
    }
}
