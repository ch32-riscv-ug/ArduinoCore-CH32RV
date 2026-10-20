#include "../../unit/core_redesign/deadline_timer.hpp"

volatile uint32_t operation, now, argument[2], observation;
#if PART == 1
experiment::DeadlineTimer timer;
#endif
extern "C" __attribute__((section(".text.entry"))) void data_entry() {
    for (;;) {
#if PART == 1 && USED
        switch (operation % 5) {
        case 0: observation = timer.arm(now, argument[0], VARIANT == 0 ? argument[1] : VARIANT == 1 ? 0 : 10); break;
        case 1: observation = timer.update(now); break;
        case 2: {
            experiment::DeadlineTimer::Expiration value;
            observation = timer.take(value) ? value.latest_due ^ value.skipped ^ uint32_t(value.saturated) : 0;
            break;
        }
        case 3: observation = timer.stop(); break;
        default: observation = timer.deadline() ^ timer.period() ^ uint32_t(timer.armed()) ^ (uint32_t(timer.ready()) << 1); break;
        }
#else
        observation = argument[0] ^ operation ^ now;
#endif
    }
}
