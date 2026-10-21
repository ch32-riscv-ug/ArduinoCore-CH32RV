#include "x0_register_wire.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>
using namespace experiment;
using namespace i2c_experiment;
#include "i2c_handoff_fake.inc"
using Resources = i2c_resource_experiment::Domain<2>;
struct Checked : Fake {
    static inline Resources* resources = nullptr;
    static inline void* holder = nullptr;
    static inline bool (*held)(void*) = nullptr;
    static void check() { assert(resources && resources->count() == 1 && resources->held() == 7 && held && held(holder)); }
    static uint16_t read16(uintptr_t p) { check(); return Fake::read16(p); }
    static void write16(uintptr_t p,uint16_t value) { check(); Fake::write16(p,value); }
    static unsigned critical_enter() { check(); return Fake::critical_enter(); }
    static void critical_leave(unsigned saved) { check(); Fake::critical_leave(saved); }
};
void stimulus(unsigned flags,unsigned status) {
    Fake::regs[S1/4] = uint16_t(flags); Fake::regs[S2/4] = uint16_t(status); Fake::seen = false;
    if (flags & SB) Fake::regs[C1/4] &= uint16_t(~START);
    if (flags & ARLO) Fake::regs[C1/4] &= uint16_t(~(START|STOP));
    if (!Fake::fifo.empty()) Fake::refresh();
}
void quiet() {
    Fake::regs[C1/4] &= uint16_t(~(START|STOP));
    stimulus(TXE,0);
}
// Host-only stimulus macros. Each poll remains an explicit bounded firmware
// operation; this helper is never linked into the target adapter.
template<class Port> void transfer(Port& port,unsigned tx,unsigned rx,uint16_t word,unsigned error,unsigned prefix) {
    if (port.driver.phase(port.request.index) == unsigned(i2c_handoff_experiment::Phase::Terminal)) { port.poll(); return; }
    if (port.driver.phase(port.request.index) == unsigned(i2c_handoff_experiment::Phase::Queued)) port.poll();
    if (port.terminal) return; // Cancelled before dispatch.
    stimulus(SB,3); port.poll(); stimulus(ADDR,3); port.poll();
    for (unsigned i = 0; i < tx; ++i) {
        if (error && i == prefix) break;
        stimulus(TXE,3); port.poll(); stimulus(BTF,3); port.poll();
    }
    if (error) { stimulus(error,3); port.poll(); }
    else if (rx) {
        stimulus(SB,3); port.poll(); stimulus(ADDR,3); port.poll();
        assert(rx == 2); Fake::fifo.push_back(uint8_t(word>>8)); Fake::fifo.push_back(uint8_t(word));
        Fake::refresh(); port.poll();
    }
    quiet(); port.poll(); assert(port.terminal && !port.driver.held());
    assert(Fake::fifo.empty());
}
template<class Driver> void peer_transfer(Driver& peer) {
    stimulus(SB,3); peer.step(); stimulus(ADDR,3); peer.step();
    stimulus(TXE,3); peer.step(); stimulus(BTF,3); peer.step(); quiet(); peer.step();
}
template<unsigned Candidate> void run(unsigned domain, bool opted) {
    RegisterX0<Candidate,Checked> app; assert(app.bus.initialize()); app.begin(WorkDomain(domain),opted);
    using Port = X0RegisterWire<Checked>;
    typename Port::Driver peer; assert(peer.open(0));
    typename Port::Driver::Storage peer_storage;
    typename Port::Driver::Token peer_request{};
    Checked::resources = &app.bus.resources;
    auto select = [&](bool other) {
        Checked::holder = other ? static_cast<void*>(&peer) : static_cast<void*>(&app.bus.driver);
        Checked::held = [](void* pointer) { return static_cast<typename Port::Driver*>(pointer)->held(); };
    };
    select(false);
    uint16_t source_bus = 10000, source_current = 1000;
    char line[160], op[32]; long a,b,c;
    while (std::fgets(line,sizeof(line),stdin)) {
        a = b = c = 0;
        if (std::sscanf(line,"%31s %ld %ld %ld",op,&a,&b,&c) < 1) continue;
        select(false); Fake::events.clear(); const auto wire_start = Fake::transmitted.size();
        if (!std::strcmp(op,"attach")) app.pps.attach();
        else if (!std::strcmp(op,"detach")) app.pps.detach();
        else if (!std::strcmp(op,"key")) app.pps.enqueue(int16_t(a));
        else if (!std::strcmp(op,"time")) app.hardware(uint32_t(a));
        else if (!std::strcmp(op,"raw")) { source_bus = uint16_t(a); source_current = uint16_t(b); }
        else if (!std::strcmp(op,"pins")) app.bus.pins.valid = a != 0;
        else if (!std::strcmp(op,"clock")) {
            if (app.bus.resources.begin_change()) assert(app.bus.resources.publish(uint32_t(a)));
        }
        else if (!std::strcmp(op,"poll")) app.bus.poll();
        else if (!std::strcmp(op,"wire")) {
            if (app.bus.active && !app.bus.terminal && app.bus.packet.kind == X0WireKind::I2c) {
                const auto packet = app.bus.packet;
                transfer(app.bus, packet.tx_size, packet.rx_size,
                         packet.tx[0] == 2 ? source_bus : source_current, unsigned(a), unsigned(b));
            }
        }
        else if (!std::strcmp(op,"peer_open")) { select(true); peer.open_backend(app.bus.resources,app.bus.pins,400000); }
        else if (!std::strcmp(op,"peer_start")) {
            select(true); peer_storage.write(0,0x55);
            assert(peer.submit(0,&peer_storage,1,0,0x41,peer_request) == Start::Accepted);
            typename Port::Driver::Token dispatched{}; assert(peer.dispatch(dispatched) == i2c_handoff_experiment::Dispatch::Started);
        }
        else if (!std::strcmp(op,"peer_finish")) {
            select(true); peer_transfer(peer); assert(peer.reap());
            Completion done; assert(peer.take(peer_request,done)); assert(done.result == Result::Success); assert(peer.close_backend());
        }
        else if (!std::strcmp(op,"peer_close")) { select(true); peer.close_backend(); }
        else if (!std::strcmp(op,"cancel")) app.bus.cancel();
        else if (!std::strcmp(op,"finish")) app.pps.completion(uint32_t(a),uint32_t(b),c != 0);
        else if (!std::strcmp(op,"step")) app.opportunity(WorkOpportunity::Explicit,WorkProfile::M);
        else if (!std::strcmp(op,"probe_prepare")) app.screen.prepare_chunk();
        else if (!std::strcmp(op,"probe_busy")) { const uint8_t byte = 0; app.bus.start(1,X0WireKind::I2c,0x3c,&byte,1,0); }
        else {
            assert(!std::strcmp(op,"opportunity"));
            app.opportunity(WorkOpportunity(a),WorkProfile(b),WorkContext(c));
        }
        app.changed();
        std::printf("{\"time\":%u,\"desired\":%d,\"confirmed\":%d,\"valid\":%d,"
            "\"pd_started\":%u,\"pd_reclaimed\":%u,\"accepted\":%u,\"rejected\":%u,"
            "\"owner\":%u,\"active\":%d,\"terminal\":%d,\"started\":%u,\"completed\":%u,\"reclaimed\":%u,"
            "\"display_starts\":%u,\"measure_starts\":%u,\"touch_starts\":%u,\"corruptions\":%u,"
            "\"revision\":%u,\"displayed_revision\":%u,\"frames\":%u,\"displayed_hash\":%u,"
            "\"measure_done\":%u,\"touch_done\":%u,\"mv\":%d,\"ma\":%d,\"stale\":%d,"
            "\"missed_measure\":%u,\"missed_touch\":%u,\"offset\":%u,\"frame_revision\":%u,\"frame\":%d,"
            "\"raw_words\":[%u,%u],\"measurement_data\":[",
            app.now,app.pps.desired,app.pps.confirmed,app.pps.valid,app.pps.started,app.pps.reclaimed,
            app.pps.accepted_count,app.pps.rejected_count,unsigned(app.bus.owner),app.bus.active,app.bus.terminal,
            app.bus.started,app.bus.completed,app.bus.reclaimed,app.bus.owner_starts[1],app.bus.owner_starts[2],app.bus.owner_starts[3],app.bus.corruptions,
            app.screen.revision,app.screen.displayed_revision,app.screen.frame_count,app.screen.displayed_hash,
            app.measurement_done,app.touch_done,app.pps.measured_mv,app.pps.measured_ma,app.pps.stale,
            app.missed_measure,app.missed_touch,unsigned(app.screen.offset),app.screen.frame_revision,app.screen.frame,
            unsigned(app.bus_word),unsigned(app.current_word));
        for (unsigned i = 0; i < 8; ++i) std::printf("%s%u",i?",":"",unsigned(app.measurement_data[i]));
        const auto& packet = app.bus.packet;
        std::printf("],\"packet\":{\"owner\":%u,\"kind\":%u,\"address\":%u,\"rx_size\":%u,\"tx\":[",
            unsigned(packet.owner),unsigned(packet.kind),unsigned(packet.address),unsigned(packet.rx_size));
        for (unsigned i = 0; i < packet.tx_size; ++i) std::printf("%s%u",i?",":"",unsigned(packet.tx[i]));
        std::printf(" ]},\"loan\":%u,\"held\":[%u,%u],\"resources\":[%u,%u,%u],\"backend\":[%u,%u,%u],\"completion\":[%u,%u,%u,%u,%u],\"wire_tx\":[",
            unsigned(app.bus.storage.available()),unsigned(app.bus.driver.held()),unsigned(peer.held()),
            app.bus.resources.frequency(),app.bus.resources.count(),app.bus.resources.held(),
            unsigned(app.bus.driver.backend_opened()),unsigned(app.bus.driver.backend_active()),app.bus.driver.backend_phase(),
            unsigned(app.bus.completion.result),unsigned(app.bus.completion.issued),unsigned(app.bus.completion.confirmed),
            unsigned(app.bus.completion.received),unsigned(app.bus.completion.flags));
        for (unsigned i = unsigned(wire_start); i < Fake::transmitted.size(); ++i) std::printf("%s%u",i == wire_start ? "" : ",",Fake::transmitted[i]);
        std::printf("],\"fault\":%u,\"events\":[",unsigned(app.bus.fault));
        for (unsigned i = 0; i < Fake::events.size(); ++i) {
            const auto& event = Fake::events[i];
            std::printf("%s[%u,%u,%u]",i?",":"",event.kind,event.offset,event.value);
        }
        std::puts("]}");
    }
}
int main(int argc,char** argv) {
    if (argc == 2 && !std::strcmp(argv[1],"contracts")) {
        X0RegisterWire<Checked> port;
        assert(!port.initialize(1234) && !port.resources.changing());
        assert(port.initialize() && !port.initialize());
        const uint8_t value = 0;
        assert(!port.start(0,X0WireKind::I2c,0x3c,&value,1,0));
        assert(!port.start(1,X0WireKind::I2c,0x01,&value,1,0));
        assert(!port.start(1,X0WireKind::I2c,0x3c,nullptr,1,0));
        assert(!port.start(1,X0WireKind::I2c,0x3c,&value,34,0));
        assert(!port.start(1,X0WireKind::VirtualTouch,0,&value,8,0));
        assert(!port.active && !port.resources.count() && port.storage.available() && Fake::events.empty());
        return 0;
    }
    assert(argc == 4);
    unsigned domain = unsigned(std::strtoul(argv[2],nullptr,10)); bool opted = std::strtoul(argv[3],nullptr,10) != 0;
    if (!std::strcmp(argv[1],"R")) run<1>(domain,opted);
    else if (!std::strcmp(argv[1],"A")) run<2>(domain,opted);
    else if (!std::strcmp(argv[1],"B")) run<3>(domain,opted);
    else { assert(!std::strcmp(argv[1],"C")); run<4>(domain,opted); }
}
