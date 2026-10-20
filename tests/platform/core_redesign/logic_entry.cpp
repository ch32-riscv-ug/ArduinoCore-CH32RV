// Link-only ABI check of the R logic with an externally driven mailbox.
// No startup, IRQ vectors, clocks, I2C or USB-PD hardware. Do not flash this ELF.
#include "../../unit/core_redesign/r_workloads.hpp"
#include "../../unit/core_redesign/r_application.hpp"

struct Mailbox { uint32_t operation; int32_t a, b, c; };
volatile Mailbox mailbox;
volatile uint32_t observation;

#if defined(P2_KNOB)
p2::Knob knob;
#elif defined(P2_APP)
p2::Application app;
#else
p2::Pps pps;
p2::Screen screen;
#endif

extern "C" __attribute__((section(".text.entry"))) void p2_logic_entry() {
    for (;;) {
        const uint32_t op = mailbox.operation;
        const int32_t a = mailbox.a, b = mailbox.b, c = mailbox.c;
#if defined(P2_KNOB)
        switch (op) {
        case 1: knob.input(static_cast<int16_t>(a)); break;
        case 2: knob.begin_write(); break;
        case 3: knob.byte(static_cast<uint8_t>(a)); break;
        case 4: knob.stop(); break;
        case 5: knob.restart(); break;
        case 6: knob.begin_read(); break;
        case 7: knob.read(); break;
        case 8: knob.step(); break;
        }
        observation = uint32_t(knob.position) + knob.sequence + knob.status + uint32_t(knob.reply);
#elif defined(P2_APP)
        switch (op) {
        case 1: app.pps.attach(); break;
        case 2: app.pps.detach(); break;
        case 3: app.pps.enqueue(static_cast<int16_t>(a)); break;
        case 4: app.hardware(uint32_t(a)); break;
        case 5: app.pps.completion(uint32_t(a), uint32_t(b), c != 0); break;
        case 6: app.source_mv = a; app.source_ma = b; break;
        case 7: app.step(); break;
        case 8: app.touch_enabled = a != 0; break;
        case 9: if (a >= 1 && a <= 3) app.bus.fail_next[a] = true; break;
        }
        observation = app.pps.started + app.screen.displayed_hash + app.measurement_done +
                      app.touch_done + app.bus.corruptions;
#else
        switch (op) {
        case 1: pps.attach(); break;
        case 2: pps.detach(); break;
        case 3: pps.enqueue(static_cast<int16_t>(a)); break;
        case 4: pps.hardware(uint32_t(a)); screen.hardware(uint32_t(a)); break;
        case 5: pps.completion(uint32_t(a), uint32_t(b), c != 0); break;
        case 6: pps.measurement(a != 0, b, c); break;
        case 7: pps.step(); break;
        case 8: screen.update({pps.desired, pps.confirmed, pps.measured_mv, pps.measured_ma,
                               pps.valid, pps.stale, pps.connected}); break;
        case 9: screen.step(); break;
        }
        observation = uint32_t(pps.confirmed) + pps.started + screen.displayed_hash;
#endif
        if (op) mailbox.operation = 0;
    }
}
