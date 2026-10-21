#include "../../unit/core_redesign/x0_register_wire.hpp"
// Link-only structural probe. Exclusive peripheral/clock/pins and IRQ-off are
// caller preconditions; these barriers do not implement interrupt exclusion.
struct Registers {
    static uint16_t read16(uintptr_t p) { return *reinterpret_cast<volatile uint16_t*>(p); }
    static void write16(uintptr_t p,uint16_t value) { *reinterpret_cast<volatile uint16_t*>(p) = value; }
    static unsigned critical_enter() { asm volatile("":::"memory"); return 0; }
    static void critical_leave(unsigned) { asm volatile("":::"memory"); }
};
#if CASE > 0
experiment::RegisterX0<FAMILY,Registers> application_probe;
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation, argument[4], observed[12];
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main() {
#if CASE > 0 && !UNUSED
    application_probe.bus.initialize(); application_probe.begin();
#endif
    for (;;) {
#if CASE > 0 && !UNUSED
        auto& app = application_probe;
        switch (operation) {
        case 0: app.pps.attach(); break;
        case 1: app.pps.detach(); break;
        case 2: observed[0] = app.pps.enqueue(int16_t(argument[0])); break;
        case 3: app.hardware(argument[0]); break;
        case 4: app.bus.pins.valid = argument[0] != 0; break;
        case 5: app.bus.poll(); break;
        case 8: observed[0] = app.bus.cancel(); break;
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
