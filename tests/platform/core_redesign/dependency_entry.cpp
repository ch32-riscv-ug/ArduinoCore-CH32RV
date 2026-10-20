#include "../../unit/core_redesign/wait_dependencies.hpp"

volatile uint32_t operation, argument[4], observation;
#if PART == 1 || PART == 2
experiment::WaitDependencies<CAPACITY, WIDTH> dependencies;
#endif
#if PART == 2 || PART == 3
uint32_t ticks = 0;
bool ready = false;
bool attempt() { if (!ready) return false; ready = false; return true; }
experiment::WaitOutcome blocking() {
    experiment::WaitPermissions p;
    p.opportunity = experiment::WorkOpportunity((argument[3] >> 2) % 3);
    p.profile = experiment::WorkProfile((argument[3] >> 4) % 3);
    p.user_opted = argument[3] & 64;
    p.clock_running = true; p.irq_enabled = true; p.remote_running = true;
    auto drive = [] { ++ticks; ready = ticks >= argument[1]; return true; };
#if PART == 2
    dependencies.actors[1].producer = experiment::WaitProducer(argument[3] & 3);
    dependencies.actors[1].opted = p.user_opted;
    experiment::DependencyDrive<decltype(dependencies), decltype(drive)> guard{dependencies, p, 1, 0, drive};
    const auto result = experiment::wait_for(attempt, [&] { return guard(); }, [] { return ticks; }, argument[0],
        experiment::WorkContext((argument[3] >> 8) & 3), dependencies.actors[1].producer, p);
    observation = unsigned(guard.last) ^ guard.checks;
    return result;
#else
    return experiment::wait_for(attempt, drive, [] { return ticks; }, argument[0],
        experiment::WorkContext((argument[3] >> 8) & 3), experiment::WaitProducer(argument[3] & 3), p);
#endif
}
#endif
extern "C" __attribute__((section(".text.entry"))) void data_entry() {
    for (;;) {
#if (PART == 1 || PART == 2) && USED
        const unsigned actor = argument[0] % CAPACITY;
        switch (operation % 4) {
        case 0: dependencies.actors[actor].needs = argument[1]; break;
        case 1: dependencies.actors[actor].waits_for = argument[1]; break;
        case 2: dependencies.owners[argument[0] % WIDTH] = int8_t(int(argument[1] % (CAPACITY + 2)) - 2); break;
        default:
#if PART == 1
            {
                experiment::WaitPermissions p;
                p.opportunity = experiment::WorkOpportunity((argument[3] >> 2) % 3);
                p.profile = experiment::WorkProfile((argument[3] >> 4) % 3);
                p.user_opted = argument[3] & 64; p.irq_enabled = true; p.remote_running = true;
                dependencies.actors[actor].producer = experiment::WaitProducer(argument[3] & 3);
                dependencies.actors[actor].opted = p.user_opted;
                dependencies.actors[actor].known = argument[3] & 128;
                observation = unsigned(dependencies.inspect(1, 0, p));
            }
#else
            { const auto r = blocking(); observation ^= unsigned(r.status) ^ r.elapsed; }
#endif
            break;
        }
#elif PART == 3 && USED
        if (operation == 0) { ticks = 0; ready = false; }
        else { const auto r = blocking(); observation = unsigned(r.status) ^ r.elapsed; }
#else
        observation = argument[0] ^ operation;
#endif
    }
}
