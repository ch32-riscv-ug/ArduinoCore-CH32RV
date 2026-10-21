#include "../../unit/core_redesign/k0_slave_register.hpp"
#include "irq_gate.hpp"
struct ProbeGate {
    using Token=unsigned;
    static void enter(Token& saved) {
        experiment::QingkeIrqGate<X035 ? 0x800 : 0x300> gate;
        gate.try_enter(saved);
#if !X035
        asm volatile("fence.i" ::: "memory");
#endif
    }
    static void leave(Token saved) {
        experiment::QingkeIrqGate<X035 ? 0x800 : 0x300> gate;gate.leave(saved);
    }
    static void point(unsigned,unsigned) {}
    static void normal() {}
};
// Structural link probe only. It does not prepare clock/pins, own a vector or
// execute on hardware. Both chosen profiles use these I2C register offsets.
struct Mmio {
    static volatile uint16_t& reg(unsigned offset) {
        return *reinterpret_cast<volatile uint16_t*>(0x40005400u+offset);
    }
    static uint16_t read_star1() { return reg(0x14); }
    static uint16_t read_star2() { return reg(0x18); }
    static uint16_t read_data() { return reg(0x10); }
    static void write_data(uint8_t value) { reg(0x10)=value; }
    static uint16_t read_control() { return reg(0); }
    static void write_control(uint16_t value) { reg(0)=value; }
    static void clear_errors(uint16_t flags) { reg(0x14)=uint16_t(~flags); }
};
#if CASE > 0
using Application=experiment::K0Shared<FAMILY,CAPACITY,ProbeGate>;
Application application_probe;
experiment::K0SlaveRegister<Application,Mmio,ADMISSION,CAPACITY> port_probe;
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
        auto& app=application_probe;Mmio io;
        switch(operation) {
        case 0:port_probe.service(app,io);break;
        case 1:app.input(int16_t(argument[0]));break;
        case 2:observed[0]=unsigned(app.opportunity(experiment::WorkOpportunity::Explicit,experiment::WorkProfile::M));break;
        case 3:observed[0]=port_probe.rearm(app,io);break;
        default:observed[0]=port_probe.recover(app,io);break;
        }
        observed[1]=uint32_t(app.position);observed[2]=uint16_t(app.increment);
        observed[3]=uint16_t(app.lower);observed[4]=uint16_t(app.upper);observed[5]=app.sequence;
        observed[6]=app.status;observed[7]=uint16_t(app.delta);observed[8]=app.inbox.size();
        observed[9]=unsigned(port_probe.mode);observed[10]=uint32_t(app.accepted);
        observed[11]=app.published[argument[0]%8]^app.held[argument[1]%8];
#else
        asm volatile("");
#endif
    }
}
