// Text protocol for deterministic host stimuli; never included in MCU builds.
#include "r_workloads.hpp"
#include "r_application.hpp"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    p2::Knob knob;
    p2::Pps pps;
    p2::Screen screen;
    p2::Application app;
    char line[160], op[32];
    long a = 0, b = 0, c = 0;
    while (std::fgets(line, sizeof(line), stdin)) {
        a = b = c = 0;
        if (std::sscanf(line, "%31s %ld %ld %ld", op, &a, &b, &c) < 1) continue;
        bool known = true;
        if (std::strcmp(argv[1], "app") == 0) {
            if (!std::strcmp(op, "attach")) app.pps.attach();
            else if (!std::strcmp(op, "detach")) app.pps.detach();
            else if (!std::strcmp(op, "key")) app.pps.enqueue(int16_t(a));
            else if (!std::strcmp(op, "time")) app.hardware(uint32_t(a));
            else if (!std::strcmp(op, "source")) { app.source_mv = int32_t(a); app.source_ma = int32_t(b); }
            else if (!std::strcmp(op, "fault") && a >= 1 && a <= 3) app.bus.fail_next[a] = true;
            else if (!std::strcmp(op, "touch")) app.touch_enabled = a != 0;
            else if (!std::strcmp(op, "corrupt")) app.screen.tx[0] ^= 1;
            else if (!std::strcmp(op, "probe_busy")) app.bus.try_start(1, app.screen.tx, 32);
            else if (!std::strcmp(op, "finish")) app.pps.completion(uint32_t(a), uint32_t(b), c != 0);
            else if (!std::strcmp(op, "step")) app.step();
            else known = false;
            if (!known) return 2;
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
        } else if (std::strcmp(argv[1], "render") == 0) {
            if (std::strcmp(op, "view")) return 2;
            const p2::View view{int32_t(a), int32_t(b), 5000, 1000, c != 0, false, true};
            for (unsigned i = 0; i < 1024; ++i) std::printf("%02x", unsigned(p2::render_byte(view, i)));
            std::puts("");
        } else if (std::strcmp(argv[1], "knob") == 0) {
            if (!std::strcmp(op, "input")) knob.input(static_cast<int16_t>(a));
            else if (!std::strcmp(op, "begin_write")) knob.begin_write();
            else if (!std::strcmp(op, "byte")) knob.byte(static_cast<uint8_t>(a));
            else if (!std::strcmp(op, "stop")) knob.stop();
            else if (!std::strcmp(op, "restart")) knob.restart();
            else if (!std::strcmp(op, "begin_read")) knob.begin_read();
            else if (!std::strcmp(op, "read")) knob.read();
            else if (!std::strcmp(op, "step")) knob.step();
            else known = false;
            if (!known) return 2;
            std::printf("%ld %d %d %d %u %u %d %d %d %u %d %d\n", long(knob.position),
                knob.increment, knob.lower, knob.upper, knob.sequence, knob.status, knob.delta,
                knob.pending, knob.reading, knob.read_index, knob.reply, knob.accepted);
        } else if (std::strcmp(argv[1], "pps") == 0) {
            if (!std::strcmp(op, "attach")) pps.attach();
            else if (!std::strcmp(op, "detach")) pps.detach();
            else if (!std::strcmp(op, "key")) pps.enqueue(static_cast<int16_t>(a));
            else if (!std::strcmp(op, "time")) pps.hardware(static_cast<uint32_t>(a));
            else if (!std::strcmp(op, "finish")) pps.completion(uint32_t(a), uint32_t(b), c != 0);
            else if (!std::strcmp(op, "sample")) pps.measurement(a != 0, int32_t(b), int32_t(c));
            else if (!std::strcmp(op, "step")) pps.step();
            else known = false;
            if (!known) return 2;
            std::printf("%lu %d %ld %d %ld %ld %lu %d %d %lu %lu %lu %lu %ld %ld %d\n",
                (unsigned long)pps.session, pps.connected, long(pps.desired), pps.valid,
                long(pps.confirmed), long(pps.sent), (unsigned long)pps.request_id,
                pps.active, pps.terminal, (unsigned long)pps.started, (unsigned long)pps.reclaimed,
                (unsigned long)pps.accepted_count, (unsigned long)pps.rejected_count,
                long(pps.measured_mv), long(pps.measured_ma), pps.stale);
        } else if (std::strcmp(argv[1], "screen") == 0) {
            if (!std::strcmp(op, "view")) screen.update({int32_t(a), int32_t(b), 5000, 1000, c != 0, false, true});
            else if (!std::strcmp(op, "time")) screen.hardware(uint32_t(a));
            else if (!std::strcmp(op, "fail")) screen.finish(false);
            else if (!std::strcmp(op, "step")) screen.step();
            else known = false;
            if (!known) return 2;
            uint32_t tx_hash = 2166136261u;
            for (uint8_t value : screen.tx) tx_hash = (tx_hash ^ value) * 16777619u;
            std::printf("%lu %lu %lu %u %d %d %lu %lu %lu\n", (unsigned long)screen.revision,
                (unsigned long)screen.frame_revision, (unsigned long)screen.displayed_revision,
                screen.offset, screen.active, screen.terminal, (unsigned long)tx_hash,
                (unsigned long)screen.displayed_hash, (unsigned long)screen.frame_count);
        } else return 2;
    }
}
