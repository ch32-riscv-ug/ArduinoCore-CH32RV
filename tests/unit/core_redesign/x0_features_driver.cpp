#include "x0_features.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>
using namespace experiment;
template<bool Runtime, class App> uint32_t touch_count(const App& app) {
    if constexpr (Runtime) return app.touch_done; else return app.touch_count();
}
template<bool Runtime, class App> uint32_t touch_missed(const App& app) {
    if constexpr (Runtime) return app.missed_touch; else return app.touch_missed();
}
template<bool Runtime, class App> uint32_t touch_starts(const App& app) {
    if constexpr (Runtime) return app.bus.owner_starts[3]; else return app.touch_starts();
}
template<class T, class = void> struct HasTouchStorage : std::false_type {};
template<class T> struct HasTouchStorage<T, std::void_t<decltype(std::declval<T>().touch_data)>> : std::true_type {};
template<unsigned Candidate> struct RuntimeType;
template<> struct RuntimeType<1> { using Type = X0R; };
template<> struct RuntimeType<2> { using Type = X0A; };
template<> struct RuntimeType<3> { using Type = X0B; };
template<> struct RuntimeType<4> { using Type = X0C; };
template<class App, bool Runtime> void protocol(WorkDomain domain, bool opt, bool enabled) {
    App app; app.begin(domain, opt);
    if constexpr (Runtime) app.touch_enabled = enabled;
    char line[160], op[32]; long a = 0, b = 0, c = 0;
    while (std::fgets(line, sizeof(line), stdin)) {
        a = b = c = 0;
        if (std::sscanf(line, "%31s %ld %ld %ld", op, &a, &b, &c) < 1) continue;
        if (!std::strcmp(op, "attach")) app.pps.attach();
        else if (!std::strcmp(op, "detach")) app.pps.detach();
        else if (!std::strcmp(op, "key")) app.pps.enqueue(int16_t(a));
        else if (!std::strcmp(op, "time")) app.hardware(uint32_t(a));
        else if (!std::strcmp(op, "source")) { app.source_mv = int32_t(a); app.source_ma = int32_t(b); }
        else if (!std::strcmp(op, "fault")) { assert(a >= 1 && a <= (enabled ? 3 : 2)); app.bus.fail_next[a] = true; }
        else if (!std::strcmp(op, "touch")) { assert((a != 0) == enabled); }
        else if (!std::strcmp(op, "corrupt")) app.screen.tx[0] ^= 1;
        else if (!std::strcmp(op, "probe_busy")) app.bus.try_start(1, app.screen.tx, 32);
        else if (!std::strcmp(op, "finish")) app.pps.completion(uint32_t(a), uint32_t(b), c != 0);
        else if (!std::strcmp(op, "step")) app.opportunity(WorkOpportunity::Explicit, WorkProfile::M);
        else { assert(!std::strcmp(op, "opportunity")); app.opportunity(WorkOpportunity(a), WorkProfile(b), WorkContext(c)); }
        app.changed();
            std::printf("{\"time\":%u,\"desired\":%d,\"confirmed\":%d,\"valid\":%d,"
                "\"pd_started\":%u,\"pd_reclaimed\":%u,\"accepted\":%u,\"rejected\":%u,"
                "\"owner\":%u,\"active\":%d,\"terminal\":%d,\"started\":%u,\"completed\":%u,\"reclaimed\":%u,"
                "\"display_starts\":%u,\"measure_starts\":%u,\"touch_starts\":%u,\"corruptions\":%u,"
                "\"revision\":%u,\"displayed_revision\":%u,\"frames\":%u,\"displayed_hash\":%u,"
                "\"measure_done\":%u,\"touch_done\":%u,\"mv\":%d,\"ma\":%d,\"stale\":%d,"
                "\"missed_measure\":%u,\"missed_touch\":%u}\n",
                app.now, app.pps.desired, app.pps.confirmed, app.pps.valid, app.pps.started, app.pps.reclaimed,
                app.pps.accepted_count, app.pps.rejected_count, unsigned(app.bus.owner), app.bus.active,
                app.bus.terminal, app.bus.started, app.bus.completed, app.bus.reclaimed,
                app.bus.owner_starts[1], app.bus.owner_starts[2], touch_starts<Runtime>(app), app.bus.corruptions,
                app.screen.revision, app.screen.displayed_revision, app.screen.frame_count, app.screen.displayed_hash,
                app.measurement_done, touch_count<Runtime>(app), app.pps.measured_mv, app.pps.measured_ma, app.pps.stale,
                app.missed_measure, touch_missed<Runtime>(app));
    }
}
template<unsigned Candidate> void select(const char* mode, WorkDomain domain, bool opted) {
    static_assert(!HasTouchStorage<FixedX0<Candidate, false>>::value, "absent feature owns no touch buffer");
    static_assert(HasTouchStorage<FixedX0<Candidate, true>>::value, "present feature owns its touch buffer");
    static_assert(sizeof(FixedX0<Candidate, false>) == sizeof(ManualX0<Candidate>), "no disabled feature RAM tax");
    if (!std::strcmp(mode, "off")) protocol<FixedX0<Candidate, false>, false>(domain, opted, false);
    else if (!std::strcmp(mode, "on")) protocol<FixedX0<Candidate, true>, false>(domain, opted, true);
    else if (!std::strcmp(mode, "manual")) protocol<ManualX0<Candidate>, false>(domain, opted, false);
    else if (!std::strcmp(mode, "runtime0")) protocol<typename RuntimeType<Candidate>::Type, true>(domain, opted, false);
    else { assert(!std::strcmp(mode, "runtime1")); protocol<typename RuntimeType<Candidate>::Type, true>(domain, opted, true); }
}
void bus_contract() {
    FeatureBus<2> bus;
    uint8_t data[32]{}; bool ok = false;
    assert(!bus.try_start(0, data, 32) && !bus.try_start(3, data, 8));
    assert(!bus.active && !bus.borrowed && !bus.started && !bus.busy);
    assert(bus.try_start(1, data, 32));
    assert(!bus.try_start(3, data, 8));
    assert(bus.owner == 1 && bus.borrowed == data && bus.busy == 0);
    assert(!bus.try_start(2, data, 8));
    assert(bus.owner == 1 && bus.borrowed == data && bus.busy == 1);
    bus.hardware(2);
    assert(bus.terminal && !bus.take(2, ok));
    assert(bus.take(1, ok) && ok && !bus.borrowed && bus.reclaimed == 1);
    FeatureBus<3> expanded;
    assert(expanded.try_start(3, data, 8)); expanded.hardware(2);
    assert(expanded.take(3, ok) && ok && expanded.owner_starts[3] == 1);
}
int main(int argc, char** argv) {
    if (argc == 2 && !std::strcmp(argv[1], "contracts")) { bus_contract(); return 0; }
    assert(argc == 5);
    const auto domain = WorkDomain(std::strtoul(argv[3], nullptr, 10));
    const bool opted = std::strtoul(argv[4], nullptr, 10) != 0;
    if (!std::strcmp(argv[1], "R")) select<1>(argv[2], domain, opted);
    else if (!std::strcmp(argv[1], "A")) select<2>(argv[2], domain, opted);
    else if (!std::strcmp(argv[1], "B")) select<3>(argv[2], domain, opted);
    else { assert(!std::strcmp(argv[1], "C")); select<4>(argv[2], domain, opted); }
}
