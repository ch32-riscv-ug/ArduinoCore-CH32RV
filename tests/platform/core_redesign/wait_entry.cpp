#include "../../unit/core_redesign/wait_policy.hpp"

volatile uint32_t operation, argument[4], observation;
#if PART != 0
uint32_t ticks = 0;
bool ready = false;
bool attempt() { if (!ready) return false; ready = false; return true; }
#endif
#if PART == 1
// Toy progress source for assembly only, not a clock/peripheral backend.
experiment::WaitOutcome blocking() {
    experiment::WaitPermissions permissions;
    const auto flags = argument[3];
    permissions.opportunity = experiment::WorkOpportunity((flags >> 2) % 3);
    permissions.profile = experiment::WorkProfile((flags >> 4) % 3);
    permissions.user_opted = flags & 64;
    permissions.clock_running = argument[2] & 1;
    permissions.irq_enabled = argument[2] & 2;
    permissions.remote_running = argument[2] & 4;
    return experiment::wait_for(attempt, [] {
        ++ticks;
        if (ticks >= argument[1]) ready = true;
        return true;
    }, [] { return ticks; }, argument[0], experiment::WorkContext((flags >> 8) & 3),
       experiment::WaitProducer(flags & 3), permissions);
}
#elif PART == 2
experiment::AsyncWait waiting;
#endif
extern "C" __attribute__((section(".text.entry"))) void data_entry() {
    for (;;) {
#if PART == 1 && USED
        if (operation == 0) { ticks = 0; ready = false; }
        else { const auto result = blocking(); observation = unsigned(result.status) ^ result.elapsed; }
#elif PART == 2 && USED
        switch (operation % 4) {
        case 0: observation = waiting.arm(ticks, argument[0]); break;
        case 1: ticks += argument[2]; ready = ticks >= argument[1]; break;
        case 2: {
            const auto result = waiting.poll(ticks, attempt, experiment::WorkContext(argument[3] & 3));
            observation = unsigned(result.status) ^ result.elapsed; break;
        }
        default: observation = waiting.cancel(); break;
        }
#elif PART == 3 && USED
        if (operation == 0) ready = argument[0] != 0;
        else observation = attempt();
#else
        observation = argument[0] ^ operation;
#endif
    }
}
