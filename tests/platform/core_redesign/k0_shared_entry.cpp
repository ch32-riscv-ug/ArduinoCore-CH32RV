#include "../../unit/core_redesign/k0_shared.hpp"
#include "irq_gate.hpp"
struct ProbeGate {
    using Token=unsigned;
    static void enter(Token& saved) {
#if PROTECTED
        experiment::QingkeIrqGate<X035 ? 0x800 : 0x300> gate;
        gate.try_enter(saved);
        // The fixed V003 SDK also fences after disabling. The older shared
        // probe's V003 policy omitted this instruction; it is not changed here.
#if !X035
        asm volatile("fence.i" ::: "memory");
#endif
#else
        saved=0; asm volatile("" ::: "memory");
#endif
    }
    static void leave(Token saved) {
#if PROTECTED
        experiment::QingkeIrqGate<X035 ? 0x800 : 0x300> gate; gate.leave(saved);
#else
        (void)saved; asm volatile("" ::: "memory");
#endif
    }
    static void point(unsigned,unsigned) {}
    static void normal() {}
};
#if CASE > 0
experiment::K0Shared<FAMILY,CAPACITY,ProbeGate> application_probe;
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation,argument[4],observed[12];
#endif
extern "C" __attribute__((section(".text.entry"),noreturn)) void probe_main() {
#if CASE > 0 && !UNUSED
    application_probe.begin();
#endif
    for (;;) {
#if CASE > 0 && !UNUSED
        auto& app=application_probe;
        switch(operation) {
        case 0:app.input(int16_t(argument[0]));break;
        case 1:app.begin_write();break;
        case 2:app.byte(uint8_t(argument[0]));break;
        case 3:app.stop();break;
        case 4:app.restart();break;
        case 5:app.begin_read();break;
        case 6:app.read();break;
        default:observed[0]=unsigned(app.opportunity(experiment::WorkOpportunity::Explicit,experiment::WorkProfile::M));break;
        }
        // Structural observation only: this probe is not a live IRQ firmware.
        observed[1]=uint32_t(app.position); observed[2]=uint16_t(app.increment);
        observed[3]=uint16_t(app.lower); observed[4]=uint16_t(app.upper); observed[5]=app.sequence;
        observed[6]=app.status; observed[7]=uint16_t(app.delta); observed[8]=app.inbox.size();
        observed[9]=uint32_t(app.reply); observed[10]=uint32_t(app.accepted);
        observed[11]=app.published[argument[0]%8] ^ app.held[argument[1]%8];
#else
        asm volatile("");
#endif
    }
}
