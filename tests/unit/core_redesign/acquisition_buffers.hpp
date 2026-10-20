#pragma once
#include <stdint.h>

// Serial host/ABI experiment. Not an ADC/DMA driver or an ISR-safe API.
namespace acquisition_experiment {
enum class Phase : uint8_t { Free, Producer, Ready, Consumer };
enum class Result : uint8_t { Success, Failed, Cancelled };
enum class Admission : uint8_t { Accepted, Closed, Busy, Exhausted };

template<unsigned Samples, class Generation = uint32_t>
class Buffers {
    static_assert(Samples > 0 && Samples <= 65535, "sample count");
    static_assert(Generation(-1) > Generation(0), "unsigned generation");
public:
    struct Token { Generation generation; uint8_t index; };
    struct Completion { Token token; uint32_t capture; uint16_t count; Result result; };
    struct Snapshot {
        Generation generation;
        uint32_t capture;
        uint16_t written, count;
        Phase phase;
        Result result;
        bool cancelled, notice;
    };
private:
    struct Block {
        Generation generation = 0;
        uint32_t capture = 0;
        uint16_t written = 0, count = 0;
        Phase phase = Phase::Free;
        Result result = Result::Success;
        bool cancelled = false, notice = false;
    };
    // Layout hypothesis only; real DMA/cache alignment must be checked per chip.
    alignas(4) uint16_t samples_[2][Samples] = {};
    Block blocks_[2] = {};
    Generation generation_ = 0;
    uint8_t ready_[2] = {}, head_ = 0, size_ = 0, next_ = 0;
    bool opened_ = false;
    Block* find(Token token) {
        if (token.index > 1 || token.generation == 0) return nullptr;
        Block& b = blocks_[token.index];
        return b.generation == token.generation && b.phase != Phase::Free ? &b : nullptr;
    }
public:
    Buffers() = default;
    Buffers(const Buffers&) = delete;
    Buffers& operator=(const Buffers&) = delete;
    Buffers(Buffers&&) = delete;
    Buffers& operator=(Buffers&&) = delete;

    bool open() {
        if (opened_ || blocks_[0].phase != Phase::Free || blocks_[1].phase != Phase::Free) return false;
        opened_ = true;
        return true;
    }
    void end() {
        opened_ = false;
        for (auto& b : blocks_) {
            b.notice = false;
            if (b.phase == Phase::Producer) b.cancelled = true;
        }
    }
    Admission reserve(uint32_t capture, Token& out) {
        if (!opened_) return Admission::Closed;
        if (blocks_[0].phase != Phase::Free && blocks_[1].phase != Phase::Free) return Admission::Busy;
        if (generation_ == Generation(-1)) return Admission::Exhausted;
        const uint8_t i = blocks_[next_].phase == Phase::Free ? next_ : uint8_t(next_ ^ 1);
        Block& b = blocks_[i];
        b = Block{};
        b.generation = ++generation_;
        b.capture = capture;
        b.phase = Phase::Producer;
        next_ = i ^ 1;
        out = Token{generation_, i};
        return Admission::Accepted;
    }
    // Prefix writes instrument the fake producer; no public DMA pointer here.
    bool write(Token token, unsigned index, uint16_t sample) {
        Block* b = find(token);
        if (!b || b->phase != Phase::Producer || index >= Samples || index != b->written) return false;
        samples_[token.index][index] = sample;
        ++b->written;
        return true;
    }
    bool cancel(Token token) {
        Block* b = find(token);
        if (!b || b->phase != Phase::Producer) return false;
        b->cancelled = true;
        return true;
    }
    // quiescent is an adapter's declaration, not evidence that hardware stopped.
    // Success may win a cancellation race. Only a known prefix is readable.
    bool hardware_terminal(Token token, Result result, unsigned count, bool quiescent) {
        Block* b = find(token);
        if (!b || b->phase != Phase::Producer || !quiescent || count > b->written ||
            unsigned(result) > unsigned(Result::Cancelled)) return false;
        if (result == Result::Success && count != Samples) return false;
        if (result == Result::Cancelled && !b->cancelled) return false;
        b->count = uint16_t(count);
        b->result = result;
        b->phase = Phase::Ready;
        b->notice = opened_;
        ready_[(head_ + size_) & 1] = token.index;
        ++size_;
        return true;
    }
    bool peek_notice(Token& out) const {
        if (!opened_) return false;
        for (unsigned n = 0; n < size_; ++n) {
            const uint8_t i = ready_[(head_ + n) & 1];
            if (blocks_[i].notice) { out = Token{blocks_[i].generation, i}; return true; }
        }
        return false;
    }
    bool ack_notice(Token token) {
        Block* b = find(token);
        if (!opened_ || !b || b->phase != Phase::Ready || !b->notice) return false;
        b->notice = false;
        return true;
    }
    bool take(Completion& out) {
        if (!size_) return false;
        const uint8_t i = ready_[head_];
        head_ ^= 1;
        --size_;
        Block& b = blocks_[i];
        b.phase = Phase::Consumer;
        b.notice = false;
        out = Completion{Token{b.generation, i}, b.capture, b.count, b.result};
        return true;
    }
    bool sample(Token token, unsigned index, uint16_t& out) {
        Block* b = find(token);
        if (!b || b->phase != Phase::Consumer || index >= b->count) return false;
        out = samples_[token.index][index];
        return true;
    }
    bool release(Token token) {
        Block* b = find(token);
        if (!b || b->phase != Phase::Consumer) return false;
        b->phase = Phase::Free;
        return true;
    }
    Snapshot snapshot(unsigned i) const {
        const Block& b = blocks_[i]; // test observer: i must be 0 or 1
        return Snapshot{b.generation, b.capture, b.written, b.count, b.phase,
                        b.result, b.cancelled, b.notice};
    }
};
} // namespace acquisition_experiment
