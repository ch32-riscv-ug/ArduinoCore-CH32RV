#include "x0_wire.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace experiment;

void contracts() {
    X0WireFake bus; uint8_t tx[34]{2}, rx[8]{99}; bool ok = false;
    assert(!bus.start(0,X0WireKind::I2c,0x40,tx,1,2));
    assert(!bus.start(2,X0WireKind::I2c,128,tx,1,2));
    assert(!bus.start(2,X0WireKind::I2c,0x40,tx,34,2));
    assert(!bus.start(2,X0WireKind::I2c,0x40,nullptr,1,2));
    assert(!bus.start(2,X0WireKind::I2c,0x40,tx,1,9));
    assert(!bus.start(2,X0WireKind::VirtualTouch,0,tx,8,0));
    assert(!bus.active && !bus.started);
    assert(bus.start(2,X0WireKind::I2c,0x40,tx,1,2));
    tx[0] = 4; // Acceptance copied TX; caller storage is now free.
    assert(bus.packet.tx[0] == 2);
    assert(!bus.start(1,X0WireKind::I2c,0x3c,tx,1,0));
    assert(!bus.take(2,ok,rx,8) && rx[0] == 99);
    bus.hardware(2); assert(bus.terminal && bus.completed == 1);
    assert(!bus.take(1,ok,rx,8) && !bus.take(2,ok,rx,1));
    assert(!bus.cancel()); // Terminal is sticky.
    assert(bus.take(2,ok,rx,8) && ok && rx[0] == 0x27 && rx[1] == 0x10);
    assert(!bus.take(2,ok,rx,8) && bus.reclaimed == 1);
    assert(bus.start(2,X0WireKind::I2c,0x40,tx,1,2));
    rx[0] = 99; assert(bus.cancel()); bus.hardware(4);
    assert(bus.take(2,ok,rx,8) && !ok && rx[0] == 99);
    assert(!bus.active && bus.started == bus.completed && bus.completed == bus.reclaimed);
}

template<unsigned Candidate> void run(unsigned domain, bool opted) {
    WireX0<Candidate> app; app.begin(WorkDomain(domain),opted);
    char line[160], op[32]; long a,b,c;
    while (std::fgets(line,sizeof(line),stdin)) {
        a = b = c = 0;
        if (std::sscanf(line,"%31s %ld %ld %ld",op,&a,&b,&c) < 1) continue;
        if (!std::strcmp(op,"attach")) app.pps.attach();
        else if (!std::strcmp(op,"detach")) app.pps.detach();
        else if (!std::strcmp(op,"key")) app.pps.enqueue(int16_t(a));
        else if (!std::strcmp(op,"time")) app.hardware(uint32_t(a));
        else if (!std::strcmp(op,"raw")) { app.bus.source_bus_word = uint16_t(a); app.bus.source_current_word = uint16_t(b); }
        else if (!std::strcmp(op,"fault")) { assert(a >= 1 && a <= 3); app.bus.fail_next[a] = true; }
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
        std::puts("]}}");
    }
}
int main(int argc,char** argv) {
    if (argc == 2 && !std::strcmp(argv[1],"contracts")) { contracts(); return 0; }
    assert(argc == 4);
    unsigned domain = unsigned(std::strtoul(argv[2],nullptr,10)); bool opted = std::strtoul(argv[3],nullptr,10) != 0;
    if (!std::strcmp(argv[1],"R")) run<1>(domain,opted);
    else if (!std::strcmp(argv[1],"A")) run<2>(domain,opted);
    else if (!std::strcmp(argv[1],"B")) run<3>(domain,opted);
    else { assert(!std::strcmp(argv[1],"C")); run<4>(domain,opted); }
}
