#include "x0_candidates.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

using namespace experiment;
template<class App> void protocol(WorkDomain domain, bool opt) {
    App app; app.begin(domain, opt);
    char line[160], op[32]; long a = 0, b = 0, c = 0;
    while (std::fgets(line, sizeof(line), stdin)) {
        a = b = c = 0;
        if (std::sscanf(line, "%31s %ld %ld %ld", op, &a, &b, &c) < 1) continue;
        if (!std::strcmp(op, "attach")) app.pps.attach();
        else if (!std::strcmp(op, "detach")) app.pps.detach();
        else if (!std::strcmp(op, "key")) app.pps.enqueue(int16_t(a));
        else if (!std::strcmp(op, "time")) app.hardware(uint32_t(a));
        else if (!std::strcmp(op, "source")) { app.source_mv = int32_t(a); app.source_ma = int32_t(b); }
        else if (!std::strcmp(op, "fault")) { assert(a >= 1 && a <= 3); app.bus.fail_next[a] = true; }
        else if (!std::strcmp(op, "touch")) app.touch_enabled = a != 0;
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
                app.bus.owner_starts[1], app.bus.owner_starts[2], app.bus.owner_starts[3], app.bus.corruptions,
                app.screen.revision, app.screen.displayed_revision, app.screen.frame_count, app.screen.displayed_hash,
                app.measurement_done, app.touch_done, app.pps.measured_mv, app.pps.measured_ma, app.pps.stale,
                app.missed_measure, app.missed_touch);
    }
}
void batch_contract() {
    using Mailbox = StaticMailbox<unsigned, 4>;
    Mailbox mailbox;
    unsigned calls = 0;
    auto absent = [&](const Mailbox::Batch&) { ++calls; };
    assert(mailbox.send(1) == EventSend::Stale);
    assert(!mailbox.notify(1));
    assert(mailbox.deliver(WorkOpportunity::Explicit, WorkProfile::U, WorkContext::Normal, absent) == TurnStatus::NotInitialized);
    assert(mailbox.begin(WorkDomain::User, true));
    assert(!mailbox.begin());
    assert(!mailbox.notify(0));
    for (unsigned i = 1; i <= 4; ++i) assert(mailbox.send(i) == EventSend::Accepted);
    assert(mailbox.send(5) == EventSend::Full);
    assert(mailbox.notify(1) && mailbox.notify(4));
    for (auto context : {WorkContext::Setup, WorkContext::Isr})
        assert(mailbox.deliver(WorkOpportunity::Explicit, WorkProfile::U, context, absent) == TurnStatus::Forbidden);
    assert(mailbox.deliver(WorkOpportunity::Explicit, WorkProfile::U, WorkContext::Callback, absent) == TurnStatus::Reentrant);
    assert(mailbox.deliver(WorkOpportunity::Delay, WorkProfile::M, WorkContext::Normal, absent) == TurnStatus::Suppressed);
    assert(mailbox.deliver(WorkOpportunity::AfterLoop, WorkProfile::S, WorkContext::Normal, absent) == TurnStatus::Ran);
    assert(calls == 0 && mailbox.size() == 4);
    auto first = [&](const Mailbox::Batch& batch) {
        ++calls;
        assert(batch.count == 4 && batch.notifications == 5);
        for (unsigned i = 0; i < 4; ++i) assert(batch.messages[i] == i + 1);
        assert(mailbox.size() == 0);
        for (unsigned i = 10; i < 14; ++i) assert(mailbox.send(i) == EventSend::Accepted);
        assert(mailbox.send(14) == EventSend::Full);
        assert(mailbox.notify(2));
        assert(mailbox.deliver(WorkOpportunity::Explicit, WorkProfile::U, WorkContext::Normal, absent) == TurnStatus::Reentrant);
        assert(batch.count == 4 && batch.notifications == 5 && batch.messages[0] == 1);
    };
    assert(mailbox.deliver(WorkOpportunity::AfterLoop, WorkProfile::U, WorkContext::Normal, first) == TurnStatus::Ran);
    assert(calls == 1 && mailbox.size() == 4);
    auto second = [&](const Mailbox::Batch& batch) {
        ++calls; assert(batch.count == 4 && batch.notifications == 2);
        for (unsigned i = 0; i < 4; ++i) assert(batch.messages[i] == i + 10);
    };
    assert(mailbox.deliver(WorkOpportunity::Explicit, WorkProfile::M, WorkContext::Normal, second) == TurnStatus::Ran);
    assert(calls == 2 && mailbox.size() == 0);
    assert(mailbox.deliver(WorkOpportunity::Explicit, WorkProfile::M, WorkContext::Normal, absent) == TurnStatus::Ran);
    assert(calls == 2);
    assert(mailbox.notify(8));
    auto notification = [&](const Mailbox::Batch& batch) {
        ++calls; assert(batch.count == 0 && batch.notifications == 8);
    };
    mailbox.deliver(WorkOpportunity::Explicit, WorkProfile::M, WorkContext::Normal, notification);
    assert(calls == 3);
    Mailbox unopted; assert(unopted.begin(WorkDomain::User, false));
    assert(unopted.send(9) == EventSend::Accepted);
    unopted.deliver(WorkOpportunity::Delay, WorkProfile::U, WorkContext::Normal, absent);
    assert(calls == 3 && unopted.size() == 1);
    unopted.deliver(WorkOpportunity::Explicit, WorkProfile::M, WorkContext::Normal, absent);
    assert(calls == 4 && unopted.size() == 0);
}
int main(int argc, char** argv) {
    if (argc == 2 && !std::strcmp(argv[1], "batch")) { batch_contract(); return 0; }
    assert(argc == 4);
    const auto domain = WorkDomain(std::strtoul(argv[2], nullptr, 10));
    const bool opt = std::strtoul(argv[3], nullptr, 10) != 0;
    if (!std::strcmp(argv[1], "R")) protocol<X0R>(domain, opt);
    else if (!std::strcmp(argv[1], "A")) protocol<X0A>(domain, opt);
    else if (!std::strcmp(argv[1], "B")) protocol<X0B>(domain, opt);
    else { assert(!std::strcmp(argv[1], "C")); protocol<X0C>(domain, opt); }
}
