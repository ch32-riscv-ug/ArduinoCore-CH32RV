#include "x0_display.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace experiment;

template<unsigned Candidate, class Panel> void run(unsigned domain, bool opted) {
    DisplayX0<Candidate, Panel> app;
    app.begin(WorkDomain(domain), opted);
    char line[160], op[32]; long a, b, c;
    while (std::fgets(line,sizeof(line),stdin)) {
        a = b = c = 0;
        if (std::sscanf(line,"%31s %ld %ld %ld",op,&a,&b,&c) < 1) continue;
        if (!std::strcmp(op,"attach")) app.pps.attach();
        else if (!std::strcmp(op,"detach")) app.pps.detach();
        else if (!std::strcmp(op,"key")) app.pps.enqueue(int16_t(a));
        else if (!std::strcmp(op,"time")) app.hardware(uint32_t(a));
        else if (!std::strcmp(op,"source")) { app.source_mv = int32_t(a); app.source_ma = int32_t(b); }
        else if (!std::strcmp(op,"fault")) { assert(a >= 1 && a <= 3); app.bus.fail_next[a] = true; }
        else if (!std::strcmp(op,"finish")) app.pps.completion(uint32_t(a),uint32_t(b),c != 0);
        else if (!std::strcmp(op,"step")) app.opportunity(WorkOpportunity::Explicit,WorkProfile::M);
        else if (!std::strcmp(op,"corrupt")) app.screen.tx[0] ^= 1;
        else if (!std::strcmp(op,"probe_prepare")) app.screen.prepare_chunk();
        else if (!std::strcmp(op,"probe_busy")) app.bus.try_start(1,app.screen.tx,uint8_t(Panel::chunk));
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
            "\"length\":%u,\"sealed_hash\":%u}\n",
            app.now,app.pps.desired,app.pps.confirmed,app.pps.valid,app.pps.started,app.pps.reclaimed,
            app.pps.accepted_count,app.pps.rejected_count,unsigned(app.bus.owner),app.bus.active,app.bus.terminal,
            app.bus.started,app.bus.completed,app.bus.reclaimed,app.bus.owner_starts[1],app.bus.owner_starts[2],app.bus.owner_starts[3],app.bus.corruptions,
            app.screen.revision,app.screen.displayed_revision,app.screen.frame_count,app.screen.displayed_hash,
            app.measurement_done,app.touch_done,app.pps.measured_mv,app.pps.measured_ma,app.pps.stale,
            app.missed_measure,app.missed_touch,unsigned(app.screen.offset),app.screen.frame_revision,app.screen.frame,
            unsigned(app.bus.length),app.bus.sealed_hash);
    }
}
template<unsigned Candidate> void select(unsigned panel, unsigned domain, bool opted) {
    using namespace display_experiment;
    switch (panel) {
    case 0: run<Candidate,Mono64<32>>(domain,opted); break;
    case 1: run<Candidate,Compact32>(domain,opted); break;
    case 2: run<Candidate,Mono64<16>>(domain,opted); break;
    case 3: run<Candidate,Rgb96>(domain,opted); break;
    default: assert(false);
    }
}
int main(int argc,char** argv) {
    assert(argc == 5);
    const unsigned panel = unsigned(std::strtoul(argv[2],nullptr,10)), domain = unsigned(std::strtoul(argv[3],nullptr,10));
    const bool opted = std::strtoul(argv[4],nullptr,10) != 0;
    if (!std::strcmp(argv[1],"R")) select<1>(panel,domain,opted);
    else if (!std::strcmp(argv[1],"A")) select<2>(panel,domain,opted);
    else if (!std::strcmp(argv[1],"B")) select<3>(panel,domain,opted);
    else { assert(!std::strcmp(argv[1],"C")); select<4>(panel,domain,opted); }
}
