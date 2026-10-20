#if CASE == 2
#include "../../unit/core_redesign/display_parts.hpp"
#else
#include "../../unit/core_redesign/x0_display.hpp"
#endif
using namespace display_experiment;
#if PANEL == 0
using Panel = Mono64<32>;
#elif PANEL == 1
using Panel = Compact32;
#elif PANEL == 2
using Panel = Mono64<16>;
#else
using Panel = Rgb96;
#endif
#if CASE == 1
experiment::DisplayX0<FAMILY,Panel> application_probe;
#elif CASE == 2
Screen<Panel> screen_probe;
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation, argument[4], observed[12];
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main() {
#if CASE == 1 && !UNUSED
    application_probe.begin();
#endif
    for (;;) {
#if CASE == 1 && !UNUSED
        auto& app = application_probe;
        switch (operation) {
        case 0: app.pps.attach(); break;
        case 1: app.pps.detach(); break;
        case 2: observed[0] = app.pps.enqueue(int16_t(argument[0])); break;
        case 3: app.hardware(argument[0]); break;
        case 4: app.source_mv = int32_t(argument[0]); app.source_ma = int32_t(argument[1]); break;
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
#elif CASE == 2 && !UNUSED
        auto& screen = screen_probe;
        switch (operation) {
        case 0: screen.update({int32_t(argument[0]),int32_t(argument[1]),int32_t(argument[2]),int32_t(argument[3]),
                              (argument[0]&1)!=0,(argument[1]&1)!=0,(argument[2]&1)!=0}); break;
        case 1: screen.step(); break;
        case 2: observed[0] = screen.prepare_chunk(); break;
        case 3: observed[0] = screen.mark_started(); break;
        case 4: observed[0] = screen.finish((argument[0]&1)!=0); break;
        case 5: screen.now = argument[0]; break;
        }
        observed[1] = screen.revision;
        observed[2] = screen.frame_revision;
        observed[3] = screen.displayed_revision;
        observed[4] = screen.offset;
        observed[5] = screen.frame_count;
        observed[6] = screen.frame_hash;
        observed[7] = screen.displayed_hash;
        observed[8] = screen.frame | (unsigned(screen.active)<<8) | (unsigned(screen.terminal)<<16);
        observed[9] = screen.chunk_size();
        observed[10] = screen.tx[argument[0]%Panel::chunk];
        observed[11] = uint32_t(screen.frozen.mv);
#else
        asm volatile("");
#endif
    }
}
