#pragma once
#include <stdint.h>
#include <limits>
#include <type_traits>

namespace experiment {
enum class IoStart { Accepted, Busy, Invalid, Unsupported, Closed, Exhausted };
enum class IoMode { Write, Read, Unsupported };
enum class IoResult { None, Success, Failure, Cancelled };
enum class IoCancel { Requested, AlreadyRequested, AlreadyTerminal, Stale };
template<unsigned Bytes, class Generation> class IoSlot;

// Ownership instrumentation for the experiment. No raw pointer is exposed to
// clients; the backend owns a lease until explicit terminal collection.
template<unsigned Bytes> class IoBuffer {
    static_assert(Bytes > 0 && Bytes <= 65535, "bounded buffer");
    template<unsigned, class> friend class IoSlot;
    uint8_t bytes[Bytes]{};
    const void* owner = nullptr;
public:
    IoBuffer() = default;
    IoBuffer(const IoBuffer&) = delete;
    IoBuffer& operator=(const IoBuffer&) = delete;
    bool available() const { return !owner; }
    bool write(unsigned i, uint8_t value) {
        if (owner || i >= Bytes) return false;
        bytes[i] = value; return true;
    }
    bool read(unsigned i, uint8_t& value) const {
        if (owner || i >= Bytes) return false;
        value = bytes[i]; return true;
    }
};

// Serial-context backend record, not a physical peripheral driver. Its lifetime
// and the buffer's lifetime must include hardware shutdown AND collection.
// end() revokes delivery; it neither frees this record nor pretends DMA stopped.
template<unsigned Bytes, class Generation = uint32_t> class IoSlot {
    static_assert(std::is_unsigned<Generation>::value &&
                  !std::is_same<Generation, bool>::value, "unsigned generation");
public:
    using Token = Generation;
    struct Completion { IoResult result = IoResult::None; unsigned transferred = 0; uint32_t digest = 0; };
private:
    IoBuffer<Bytes>* buffer = nullptr;
    Generation generation = 0;
    unsigned length = 0;
    IoMode mode = IoMode::Write;
    Completion completion;
    bool is_open = false, is_terminal = false, cancelled = false, pending = false;
    bool matches(Token token) const { return buffer && token && token == generation; }
public:
    IoSlot() = default;
    IoSlot(const IoSlot&) = delete;
    IoSlot& operator=(const IoSlot&) = delete;
    bool opened() const { return is_open; }
    bool active() const { return buffer != nullptr; }
    bool terminal() const { return is_terminal; }
    bool cancel_pending() const { return cancelled; }
    bool notification_pending() const { return pending; }
    Token token() const { return generation; }
    IoResult result() const { return completion.result; }
    bool open() {
        if (is_open || buffer) return false;
        is_open = true; return true;
    }
    bool end() {
        if (!is_open) return false;
        is_open = false; pending = false;
        if (buffer && !is_terminal) cancelled = true;
        return true;
    }
    IoStart start(IoBuffer<Bytes>* data, unsigned size, IoMode operation, unsigned address) {
        if (!is_open) return IoStart::Closed;
        if (buffer) return IoStart::Busy;
        if (operation != IoMode::Write && operation != IoMode::Read) return IoStart::Unsupported;
        if (!data || !size || size > Bytes || address > 127) return IoStart::Invalid;
        if (!data->available()) return IoStart::Busy;
        if (generation == std::numeric_limits<Generation>::max()) return IoStart::Exhausted;
        ++generation; buffer = data; data->owner = this; length = size; mode = operation;
        completion = {}; is_terminal = cancelled = pending = false;
        return IoStart::Accepted;
    }
    IoCancel cancel(Token request) {
        if (!matches(request)) return IoCancel::Stale;
        if (is_terminal) return IoCancel::AlreadyTerminal;
        if (cancelled) return IoCancel::AlreadyRequested;
        cancelled = true; return IoCancel::Requested;
    }
    // Fake hardware's stop witness. A real adapter may invoke this ONLY after
    // all hardware accesses are quiescent. Cancellation acceptance is not one.
    bool hardware_terminal(Token request, IoResult outcome, unsigned transferred, uint8_t fill = 0) {
        if (!matches(request) || is_terminal || outcome == IoResult::None ||
            (outcome != IoResult::Success && outcome != IoResult::Failure && outcome != IoResult::Cancelled) ||
            transferred > length || (outcome == IoResult::Success && transferred != length) ||
            (outcome == IoResult::Cancelled && !cancelled)) return false;
        uint32_t digest = 2166136261u;
        for (unsigned i = 0; i < transferred; ++i) {
            if (mode == IoMode::Read) buffer->bytes[i] = uint8_t(fill + i);
            digest = (digest ^ buffer->bytes[i]) * 16777619u;
        }
        completion = {outcome, transferred, digest};
        is_terminal = true; pending = is_open;
        return true;
    }
    bool peek_notice(Token& request) const {
        if (!pending || !is_open || !is_terminal) return false;
        request = generation; return true;
    }
    bool ack_notice(Token request) {
        if (!matches(request) || !pending || !is_terminal || !is_open) return false;
        pending = false; return true;
    }
    bool take(Token request, Completion& value) {
        if (!matches(request) || !is_terminal) return false;
        value = completion;
        buffer->owner = nullptr; buffer = nullptr;
        completion = {}; length = 0; is_terminal = cancelled = pending = false;
        return true;
    }
};
} // namespace experiment
