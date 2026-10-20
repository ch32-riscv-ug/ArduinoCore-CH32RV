#include "../../unit/core_redesign/k0_candidates.hpp"

volatile uint32_t operation, argument[3], observation;
#if PART == 1
experiment::K0R<CAPACITY> application;
#elif PART == 2
experiment::K0A<CAPACITY> application;
#elif PART == 3
experiment::K0B<CAPACITY> application;
#elif PART == 4
experiment::K0C<CAPACITY> application;
#endif
extern "C" __attribute__((section(".text.entry"))) void data_entry() {
#if PART != 0 && USED
    application.begin();
#endif
    for (;;) {
#if PART != 0 && USED
        switch (operation % 8) {
        case 0: application.input(int16_t(argument[0])); break;
        case 1: application.begin_write(); break;
        case 2: application.byte(uint8_t(argument[0])); break;
        case 3: application.stop(); break;
        case 4: application.restart(); break;
        case 5: application.begin_read(); break;
        case 6: application.read(); break;
        default:
            application.opportunity(WIDTH == 0 ? experiment::WorkOpportunity::Explicit : experiment::WorkOpportunity::Delay,
                                    WIDTH == 0 ? experiment::WorkProfile::M : WIDTH == 1 ? experiment::WorkProfile::S : experiment::WorkProfile::U);
            break;
        }
        application.changed();
        uint32_t digest = uint32_t(application.position) ^ uint32_t(application.delta) ^
                          uint32_t(application.increment) ^ uint32_t(application.lower) ^ uint32_t(application.upper);
        for (auto value : application.published) digest = (digest ^ value) * 16777619u;
        observation = digest ^ application.sequence ^ application.status ^ application.inbox.size() ^
                      uint32_t(application.reply) ^ (uint32_t(application.accepted) << 8);
#else
        observation = argument[0] ^ operation;
#endif
    }
}
