#include "../../unit/core_redesign/x0_wire.hpp"
#include "../../unit/core_redesign/x0_display.hpp"
#if CASE == 1
experiment::WireX0<FAMILY> application_probe;
#elif CASE == 2
experiment::DisplayX0<FAMILY,display_experiment::Mono64<32>> application_probe;
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation, argument[4], observed[12];
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main() {
#if CASE > 0 && !UNUSED
    application_probe.begin();
#endif
    for (;;) {
#if CASE > 0 && !UNUSED
        auto& app = application_probe;
        switch (operation) {
        case 0: app.pps.attach(); break;
        case 1: app.pps.detach(); break;
        case 2: observed[0] = app.pps.enqueue(int16_t(argument[0])); break;
        case 3: app.hardware(argument[0]); break;
        case 4:
#if CASE == 1
            app.bus.source_bus_word = uint16_t(argument[0]); app.bus.source_current_word = uint16_t(argument[1]);
#else
            app.source_mv = int32_t((uint16_t(argument[0])>>3)*4);
            app.source_ma = uint16_t(argument[1]) < 32768 ? int32_t(uint16_t(argument[1])) : int32_t(uint16_t(argument[1]))-65536;
#endif
            break;
        case 5: app.bus.fail_next[argument[0]%3+1] = true; break;
        case 6: app.pps.completion(argument[0],argument[1],argument[2] != 0); break;
        default: observed[0] = unsigned(app.opportunity(experiment::WorkOpportunity::Explicit,experiment::WorkProfile::M)); break;
        }
        app.changed();
        observed[1] = uint32_t(app.pps.desired);
        observed[2] = uint32_t(app.pps.confirmed);
        observed[3] = app.pps.valid | (uint32_t(app.pps.stale)<<8);
        observed[4] = app.pps.accepted_count ^ app.pps.rejected_count ^ app.pps.started ^ app.pps.reclaimed;
        observed[5] = app.bus.started ^ app.bus.completed ^ app.bus.reclaimed ^ app.bus.corruptions;
        observed[6] = app.screen.displayed_hash;
        observed[7] = app.screen.displayed_revision ^ app.screen.revision;
        observed[8] = app.screen.frame_count;
        observed[9] = app.measurement_done;
        observed[10] = app.touch_done;
        observed[11] = app.missed_measure ^ app.missed_touch ^ uint32_t(app.pps.measured_mv) ^ uint32_t(app.pps.measured_ma);
#else
        asm volatile("");
#endif
    }
}
