// Text protocol for deterministic host stimuli; never included in MCU builds.
#include "r_workloads.hpp"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    p2::Knob knob;
    p2::Pps pps;
    p2::Screen screen;
    char line[160], op[32];
    long a = 0, b = 0, c = 0;
    while (std::fgets(line, sizeof(line), stdin)) {
        a = b = c = 0;
        if (std::sscanf(line, "%31s %ld %ld %ld", op, &a, &b, &c) < 1) continue;
        bool known = true;
        if (std::strcmp(argv[1], "render") == 0) {
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
