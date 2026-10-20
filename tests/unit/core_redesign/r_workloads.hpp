#pragma once

// P2 R control: explicit, bounded state machines. No production-core dependency.
// Host stimuli serialize the IRQ/frontend operations. Real ISR synchronization
// and peripheral backends are separate experiments, not proved by this file.
#include <stdint.h>

namespace p2 {

struct Knob {
    int32_t position = 0;
    int16_t increment = 1, lower = -1000, upper = 1000, delta = 0;
    uint16_t sequence = 0;
    uint8_t status = 0, staging[8]{}, pending_frame[8]{}, published[8]{}, held[8]{};
    uint8_t received = 0, read_index = 0;
    bool writing = false, pending = false, reading = false;
    int reply = -1, accepted = -1;

    static uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
    static int16_t i16(const uint8_t* p) {
        const int32_t value = u16(p);
        return static_cast<int16_t>(value >= 32768 ? value - 65536 : value);
    }
    void input(int16_t value) {
        const int32_t total = int32_t(delta) + value;
        if (total > 32767) { delta = 32767; status |= 1; }
        else if (total < -32768) { delta = -32768; status |= 1; }
        else delta = static_cast<int16_t>(total);
    }
    void end_read() { reading = false; }
    void abort_write() {
        if (writing) status |= 4;
        writing = false;
    }
    void begin_write() {
        abort_write();
        end_read();
        accepted = pending ? 0 : 1;
        if (pending) status |= 2;
        writing = !pending;
        received = 0;
    }
    void byte(uint8_t value) {
        if (!writing) return;
        if (received < 8) staging[received] = value;
        if (received < 9) ++received; // 9 records an overlong frame without wrapping.
    }
    void stop() {
        if (writing) {
            if (received != 8) status |= 4;
            else {
                for (unsigned i = 0; i < 8; ++i) pending_frame[i] = staging[i];
                pending = true;
            }
        }
        writing = false;
        end_read();
    }
    void restart() { abort_write(); end_read(); }
    void begin_read() {
        abort_write();
        for (unsigned i = 0; i < 8; ++i) held[i] = published[i];
        reading = true;
        read_index = 0;
    }
    void read() {
        reply = !reading || read_index >= 8 ? 0 : held[read_index];
        if (read_index < 8) ++read_index;
    }
    void step() {
        if (pending) {
            const int16_t step = i16(pending_frame + 2);
            const int16_t lo = i16(pending_frame + 4), hi = i16(pending_frame + 6);
            if (step < 1 || step > 127 || lo > hi) status |= 4;
            else {
                increment = step; lower = lo; upper = hi;
                sequence = u16(pending_frame);
            }
            pending = false;
        }
        position += int32_t(delta) * increment;
        delta = 0;
        if (position < lower) position = lower;
        if (position > upper) position = upper;
        const uint32_t encoded = static_cast<uint32_t>(position);
        for (unsigned i = 0; i < 4; ++i) published[i] = uint8_t(encoded >> (8 * i));
        published[4] = uint8_t(sequence); published[5] = uint8_t(sequence >> 8);
        published[6] = status; published[7] = 0;
    }
};

struct Pps {
    struct Command { int16_t kind; int16_t value; };
    static_assert(sizeof(Command) == 4, "X0 input payload must be four bytes");
    Command commands[4]{};
    uint32_t command_sessions[4]{}; // Envelope metadata, separate from four-byte input payload.
    uint8_t head = 0, count = 0;
    uint32_t session = 0, active_session = 0, request_id = 0;
    uint32_t accepted_count = 0, rejected_count = 0, started = 0, reclaimed = 0;
    int32_t desired = 5000, selected = 0, sent = 0, confirmed = 0;
    uint32_t now = 0, due = 0;
    bool connected = false, active = false, terminal = false, terminal_success = false;
    bool valid = false, selected_pending = false;
    int32_t measured_mv = 0, measured_ma = 0;
    bool stale = true;

    void attach() {
        ++session;
        connected = true; valid = false; selected_pending = false;
    }
    void detach() {
        connected = false; valid = false; selected_pending = false;
        if (active && !terminal) { terminal = true; terminal_success = false; }
    }
    bool enqueue(int16_t kind) {
        if (count == 4) { ++rejected_count; return false; }
        commands[(head + count) % 4] = {kind, 0};
        command_sessions[(head + count) % 4] = connected ? session : 0;
        ++count; ++accepted_count;
        return true;
    }
    void completion(uint32_t id, uint32_t connection, bool success) {
        if (!active || terminal || id != request_id || connection != active_session) return;
        terminal = true; terminal_success = success;
    }
    void hardware(uint32_t time) {
        now = time;
        if (active && !terminal && now >= due) completion(request_id, active_session, true);
    }
    void measurement(bool ok, int32_t mv, int32_t ma) {
        if (ok) { measured_mv = mv; measured_ma = ma; }
        stale = !ok;
    }
    void step() {
        // Snapshot the number of input commands; no producer is invoked here.
        const unsigned ready = count;
        for (unsigned i = 0; i < ready; ++i) {
            const int kind = commands[head].kind;
            const uint32_t input_session = command_sessions[head];
            head = uint8_t((head + 1) % 4); --count;
            if (kind == 1 && desired < 11000) desired += 20;
            if (kind == -1 && desired > 5000) desired -= 20;
            if (kind == 0 && connected && input_session == session) {
                selected = desired; selected_pending = true;
            }
        }
        if (active && terminal) {
            if (terminal_success && connected && session == active_session) {
                confirmed = sent; valid = true;
            }
            active = false; terminal = false; ++reclaimed;
        }
        if (!active && selected_pending && connected) {
            sent = selected; selected_pending = false;
            active = true; active_session = session; ++request_id; ++started;
            due = now + 5;
        }
    }
};

// Byte-exact renderer and transport are independent of the execution mechanism.
// These original glyphs are deliberately limited to the X0 displayed fields.
struct View {
    int32_t desired, confirmed, mv, ma;
    bool valid, stale, connected;
};

inline uint8_t glyph(char c, unsigned x) {
    static const uint8_t digits[10][3] = {
        {31,17,31},{18,31,16},{29,21,23},{21,21,31},{7,4,31},
        {23,21,29},{31,21,29},{1,1,31},{31,21,31},{23,21,31}
    };
    if (c >= '0' && c <= '9') return digits[c - '0'][x];
    if (c == '-') return x < 3 ? 4 : 0;
    if (c == 'D') { const uint8_t p[3] = {31,17,14}; return p[x]; }
    if (c == 'C') { const uint8_t p[3] = {31,17,17}; return p[x]; }
    if (c == 'V') { const uint8_t p[3] = {15,16,15}; return p[x]; }
    if (c == 'A') { const uint8_t p[3] = {30,5,30}; return p[x]; }
    if (c == 'S') { const uint8_t p[3] = {23,21,29}; return p[x]; }
    if (c == 'L') { const uint8_t p[3] = {31,16,16}; return p[x]; }
    return 0;
}

inline char digit_at(int32_t number, unsigned index) {
    if (number < 0) return '-'; // X0 measurement display uses '-' for negative values.
    uint32_t value = static_cast<uint32_t>(number);
    uint32_t divisor = 10000;
    for (unsigned i = 0; i < index; ++i) divisor /= 10;
    return char('0' + (value / divisor) % 10);
}

inline uint8_t render_byte(const View& view, unsigned offset) {
    const unsigned page = offset / 128, x = offset % 128;
    if (x >= 24 || page > 5 || page == 4) return 0;
    const unsigned cell = x / 4, column = x % 4;
    if (column == 3) return 0;
    char c = ' ';
    if (page == 5) {
        if (cell == 0) c = 'L';
        if (cell == 1) c = view.connected ? '1' : '0';
        if (cell == 3) c = 'S';
        if (cell == 4) c = view.stale ? '1' : '0';
    } else {
        if (cell == 0) c = page == 0 ? 'D' : page == 1 ? 'C' : page == 2 ? 'V' : 'A';
        else if (page == 1 && !view.valid) c = '-';
        else c = digit_at(page == 0 ? view.desired : page == 1 ? view.confirmed :
                          page == 2 ? view.mv : view.ma, cell - 1);
    }
    return glyph(c, column);
}

struct Screen {
    View latest{5000, 0, 0, 0, false, true, false}, frozen{};
    uint32_t revision = 1, frame_revision = 0, displayed_revision = 0;
    uint32_t now = 0, due = 0, next_frame = 0, frame_count = 0;
    uint16_t offset = 0;
    uint8_t tx[32]{};
    bool frame = false, active = false, terminal = false, success = false;
    // Rolling hash is observation only; no framebuffer is allocated in target.
    uint32_t frame_hash = 2166136261u, displayed_hash = 0;

    void update(const View& value) { latest = value; ++revision; }
    void finish(bool ok) {
        if (active && !terminal) { terminal = true; success = ok; }
    }
    void hardware(uint32_t time) {
        now = time;
        if (active && now >= due) finish(true);
    }
    void step() {
        if (active && terminal) {
            active = false; terminal = false;
            if (!success) { frame = false; next_frame = now + 100; }
            else {
                for (unsigned i = 0; i < 32; ++i) frame_hash = (frame_hash ^ tx[i]) * 16777619u;
                offset += 32;
                if (offset == 1024) {
                    displayed_revision = frame_revision; displayed_hash = frame_hash;
                    ++frame_count; frame = false;
                }
            }
        }
        if (!frame && revision != displayed_revision && now >= next_frame) {
            frozen = latest; frame_revision = revision; offset = 0;
            frame_hash = 2166136261u; frame = true; next_frame = now + 100;
        }
        if (frame && !active) {
            for (unsigned i = 0; i < 32; ++i) tx[i] = render_byte(frozen, offset + i);
            active = true; terminal = false; due = now + 2;
        }
    }
};

} // namespace p2
