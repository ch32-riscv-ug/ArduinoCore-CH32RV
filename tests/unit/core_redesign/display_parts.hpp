#pragma once
#include <stdint.h>

// Standalone experiment library: no Arduino, bus, queue, executor or clock API.
// Profiles describe mock byte streams, not real controller wire protocols.
namespace display_experiment {
struct View { int32_t desired, confirmed, mv, ma; bool valid, stale, connected; };
inline bool same(const View& a, const View& b) {
    return a.desired == b.desired && a.confirmed == b.confirmed && a.mv == b.mv && a.ma == b.ma &&
           a.valid == b.valid && a.stale == b.stale && a.connected == b.connected;
}
inline uint8_t glyph(char c, unsigned x) {
    static const uint8_t digits[10][3] = {{31,17,31},{18,31,16},{29,21,23},{21,21,31},{7,4,31},
        {23,21,29},{31,21,29},{1,1,31},{31,21,31},{23,21,31}};
    if (c >= '0' && c <= '9') return digits[c-'0'][x];
    if (c == '-') return 4;
    if (c == 'D') { const uint8_t p[3] = {31,17,14}; return p[x]; }
    if (c == 'C') { const uint8_t p[3] = {31,17,17}; return p[x]; }
    if (c == 'V') { const uint8_t p[3] = {15,16,15}; return p[x]; }
    if (c == 'A') { const uint8_t p[3] = {30,5,30}; return p[x]; }
    if (c == 'S') { const uint8_t p[3] = {23,21,29}; return p[x]; }
    if (c == 'L') { const uint8_t p[3] = {31,16,16}; return p[x]; }
    return 0;
}
inline uint8_t mono_byte(const View& v, unsigned offset) {
    const unsigned page = offset/128, x = offset%128, cell = x/4, column = x%4;
    if (x >= 24 || page > 5 || page == 4 || column == 3) return 0;
    char c = ' ';
    if (page == 5) {
        if (cell == 0) c = 'L';
        if (cell == 1) c = v.connected ? '1' : '0';
        if (cell == 3) c = 'S';
        if (cell == 4) c = v.stale ? '1' : '0';
    } else if (cell == 0) c = page == 0 ? 'D' : page == 1 ? 'C' : page == 2 ? 'V' : 'A';
    else if (page == 1 && !v.valid) c = '-';
    else {
        const int32_t number = page == 0 ? v.desired : page == 1 ? v.confirmed : page == 2 ? v.mv : v.ma;
        if (number < 0) c = '-';
        else {
            uint32_t divisor = 10000;
            for (unsigned i = 1; i < cell; ++i) divisor /= 10;
            c = char('0' + (uint32_t(number)/divisor)%10);
        }
    }
    return glyph(c, column);
}
template<unsigned Chunk> struct Mono64 {
    static constexpr unsigned bytes = 1024, chunk = Chunk;
    static uint8_t at(const View& v, unsigned offset) { return mono_byte(v, offset); }
};
struct Compact32 {
    static constexpr unsigned bytes = 512, chunk = 31;
    static uint8_t at(const View& v, unsigned offset) {
        const unsigned x = offset%128;
        return offset < 128 && x >= 32 && x < 56 ? mono_byte(v, 5*128+x-32) : mono_byte(v, offset);
    }
};
struct Rgb96 {
    static constexpr unsigned bytes = 96*64*2, chunk = 64;
    static uint8_t at(const View& v, unsigned offset) {
        const unsigned pixel = offset/2, x = pixel%96, y = pixel/96;
        const bool lit = (mono_byte(v, (y/8)*128+x) & (1u << (y%8))) != 0;
        return lit ? (offset%2 ? 0x07 : 0xe0) : 0; // mock little-endian green RGB565
    }
};

template<class Panel> class Screen {
    static_assert(Panel::bytes > 0 && Panel::bytes <= 65535 && Panel::chunk > 0 && Panel::chunk <= 255, "bounded stream");
    uint8_t prepared = 0, submitted = 0;
public:
    View latest{5000,0,0,0,false,true,false}, frozen{};
    uint32_t revision = 1, frame_revision = 0, displayed_revision = 0;
    uint32_t now = 0, next_frame = 0, frame_count = 0;
    uint16_t offset = 0;
    uint8_t tx[Panel::chunk]{};
    bool frame = false, active = false, terminal = false, success = false;
    uint32_t frame_hash = 2166136261u, displayed_hash = 0;
    static constexpr unsigned frame_bytes = Panel::bytes, capacity = Panel::chunk;
    void update(const View& value) { latest = value; ++revision; }
    unsigned chunk_size() const {
        if (!frame || offset >= Panel::bytes) return 0;
        const unsigned left = Panel::bytes-offset;
        return left < Panel::chunk ? left : Panel::chunk;
    }
    bool prepare_chunk() {
        if (!frame || active) return false;
        prepared = uint8_t(chunk_size());
        for (unsigned i = 0; i < prepared; ++i) tx[i] = Panel::at(frozen, offset+i);
        return prepared != 0;
    }
    bool mark_started() {
        if (!frame || active || !prepared) return false;
        if (offset == 0) next_frame = now+100;
        submitted = prepared; prepared = 0;
        active = true; terminal = false;
        return true;
    }
    bool finish(bool ok) {
        if (!active || terminal) return false;
        terminal = true; success = ok; return true;
    }
    void step() {
        if (active && terminal) {
            active = terminal = false;
            if (!success) { frame = false; next_frame = now+100; }
            else {
                for (unsigned i = 0; i < submitted; ++i) frame_hash = (frame_hash ^ tx[i])*16777619u;
                offset = uint16_t(offset+submitted);
                if (offset == Panel::bytes) {
                    displayed_revision = frame_revision; displayed_hash = frame_hash;
                    ++frame_count; frame = false;
                }
            }
            submitted = 0;
        }
        if (!frame && revision != displayed_revision && now >= next_frame) {
            frozen = latest; frame_revision = revision; offset = 0;
            frame_hash = 2166136261u; frame = true; next_frame = now+100;
        }
    }
};
} // namespace display_experiment
