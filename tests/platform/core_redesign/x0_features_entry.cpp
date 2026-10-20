#include "../../unit/core_redesign/x0_features.hpp"

volatile uint32_t operation, argument[3], observation;
#if PART != 0
#if VARIANT == 0 || VARIANT == 1
#if PART == 1
using Application = experiment::X0R;
#elif PART == 2
using Application = experiment::X0A;
#elif PART == 3
using Application = experiment::X0B;
#else
using Application = experiment::X0C;
#endif
#elif VARIANT == 4
using Application = experiment::ManualX0<PART>;
#else
using Application = experiment::FixedX0<PART, VARIANT == 3>;
#endif
Application application;
static_assert(sizeof(experiment::FixedX0<PART, false>) == sizeof(experiment::ManualX0<PART>), "no disabled feature RAM tax");
#endif
extern "C" __attribute__((section(".text.entry"))) void data_entry() {
#if PART != 0 && USED
    application.begin();
#if VARIANT == 0 || VARIANT == 1
    application.touch_enabled = VARIANT == 1;
#endif
#endif
    for (;;) {
#if PART != 0 && USED
        switch (operation % 8) {
        case 0: application.pps.attach(); break;
        case 1: application.pps.detach(); break;
        case 2: application.pps.enqueue(int16_t(argument[0])); break;
        case 3: application.hardware(argument[0]); break;
        case 4: application.source_mv = int32_t(argument[0]); application.source_ma = int32_t(argument[1]); break;
        case 5: application.bus.fail_next[argument[0] % ((VARIANT == 1 || VARIANT == 3) ? 3 : 2) + 1] = true; break;
        case 6: application.pps.completion(argument[0], argument[1], argument[2] != 0); break;
        default:
            application.opportunity(WIDTH == 0 ? experiment::WorkOpportunity::Explicit : experiment::WorkOpportunity::Delay,
                                    WIDTH == 0 ? experiment::WorkProfile::M : WIDTH == 1 ? experiment::WorkProfile::S : experiment::WorkProfile::U);
            break;
        }
        application.changed();
        observation = uint32_t(application.pps.confirmed) ^ uint32_t(application.pps.desired) ^
            uint32_t(application.pps.valid) ^ application.pps.started ^ application.pps.reclaimed ^
            application.pps.accepted_count ^ application.pps.rejected_count ^ application.bus.started ^
            application.bus.completed ^ application.bus.reclaimed ^ application.bus.corruptions ^
            application.screen.displayed_hash ^ application.screen.revision ^ application.screen.frame_count ^
            application.measurement_done ^ uint32_t(application.pps.measured_mv) ^
            uint32_t(application.pps.measured_ma) ^ application.missed_measure;
#if VARIANT == 0 || VARIANT == 1
        observation ^= application.touch_done ^ application.missed_touch;
#else
        observation ^= application.touch_count() ^ application.touch_missed();
#endif
#else
        observation = argument[0] ^ operation;
#endif
    }
}
