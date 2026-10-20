#pragma once
#include <stdint.h>

// Serial contract experiment. Gate must provide all visibility/serialization
// required by its backend; this file supplies no MCU synchronization or DMA.
namespace h417_experiment {
enum class Owners : uint8_t { Single, Fixed, Shared };
enum class Phase : uint8_t { Free, Writing, Ready, Reading };
struct Message { uint32_t words[4]; };
struct Token { uint32_t domain, epoch, generation, index; };

template<Owners Policy, class Gate, class Counter = uint32_t>
class Fabric {
    static_assert(Counter(-1) > Counter(0), "unsigned counter");
    struct Guard {
        Gate& gate;
        bool ok;
        explicit Guard(Gate& g) : gate(g), ok(g.try_enter()) {}
        ~Guard() { if (ok) gate.leave(); }
    };
    struct Block {
        uint8_t bytes[64] = {};
        Counter generation = 0;
        Phase phase = Phase::Free;
        uint8_t owner = 0, written = 0;
    };
    Gate& gate_;
    const uint32_t domain_;
    Counter epoch_ = 1, generation_ = 0;
    Message messages_[4] = {};
    Block blocks_[2] = {};
    uint8_t ready_[2] = {}, head_ = 0, size_ = 0;
    uint8_t ready_head_ = 0, ready_size_ = 0, notices_ = 0, quiet_ = 0;
    bool recovering_ = false;
    bool producer(unsigned core) const {
        return core < 2 && (Policy == Owners::Shared || core == 0);
    }
    bool consumer(unsigned core) const {
        return core < 2 && (Policy == Owners::Shared ||
               core == (Policy == Owners::Single ? 0u : 1u));
    }
    bool active() const { return domain_ != 0 && !recovering_; }
    Block* find(Token t) {
        if (t.domain != domain_ || t.epoch != epoch_ || t.index >= 2 || !t.generation) return nullptr;
        Block& b = blocks_[t.index];
        return b.phase != Phase::Free && b.generation == t.generation ? &b : nullptr;
    }
public:
    constexpr explicit Fabric(Gate& gate, uint32_t domain) : gate_(gate), domain_(domain) {}
    Fabric(const Fabric&) = delete;
    Fabric& operator=(const Fabric&) = delete;
    Fabric(Fabric&&) = delete;
    Fabric& operator=(Fabric&&) = delete;
    bool push(unsigned core, const Message& m) {
        Guard g(gate_);
        if (!g.ok || !active() || !producer(core) || size_ == 4) return false;
        messages_[(head_ + size_) & 3] = m;
        ++size_; notices_ |= 1;
        return true;
    }
    bool pop(unsigned core, Message& out) {
        Guard g(gate_);
        if (!g.ok || !active() || !consumer(core) || !size_) return false;
        out = messages_[head_]; head_ = (head_ + 1) & 3; --size_;
        return true;
    }
    bool reserve(unsigned core, Token& out) {
        Guard g(gate_);
        if (!g.ok || !active() || !producer(core) || generation_ == Counter(-1)) return false;
        for (unsigned i = 0; i < 2; ++i) {
            Block& b = blocks_[i];
            if (b.phase != Phase::Free) continue;
            b.phase = Phase::Writing; b.owner = uint8_t(core); b.written = 0;
            b.generation = ++generation_;
            out = Token{domain_, epoch_, b.generation, i};
            return true;
        }
        return false;
    }
    bool write(unsigned core, Token t, unsigned index, unsigned value) {
        Guard g(gate_);
        if (!g.ok || !active() || !producer(core)) return false;
        Block* b = find(t);
        if (!b || b->phase != Phase::Writing || b->owner != core ||
            index >= 64 || index != b->written || value > 255) return false;
        b->bytes[index] = uint8_t(value); ++b->written;
        return true;
    }
    bool publish(unsigned core, Token t) {
        Guard g(gate_);
        if (!g.ok || !active() || !producer(core)) return false;
        Block* b = find(t);
        if (!b || b->phase != Phase::Writing || b->owner != core || b->written != 64) return false;
        b->phase = Phase::Ready;
        ready_[(ready_head_ + ready_size_) & 1] = uint8_t(t.index);
        ++ready_size_; notices_ |= 2;
        return true;
    }
    bool take(unsigned core, Token& out) {
        Guard g(gate_);
        if (!g.ok || !active() || !consumer(core) || !ready_size_) return false;
        unsigned i = ready_[ready_head_]; ready_head_ ^= 1; --ready_size_;
        Block& b = blocks_[i]; b.phase = Phase::Reading; b.owner = uint8_t(core);
        out = Token{domain_, epoch_, b.generation, i};
        return true;
    }
    bool read(unsigned core, Token t, unsigned index, unsigned& out) {
        Guard g(gate_);
        if (!g.ok || !active() || !consumer(core)) return false;
        Block* b = find(t);
        if (!b || b->phase != Phase::Reading || b->owner != core || index >= 64) return false;
        out = b->bytes[index]; return true;
    }
    bool release(unsigned core, Token t) {
        Guard g(gate_);
        if (!g.ok || !active() || !consumer(core)) return false;
        Block* b = find(t);
        if (!b || b->phase != Phase::Reading || b->owner != core) return false;
        b->phase = Phase::Free; return true;
    }
    bool ack(unsigned core, unsigned mask) {
        Guard g(gate_);
        if (!g.ok || !active() || !consumer(core) || mask > 3) return false;
        notices_ &= uint8_t(~mask); return true;
    }
    bool begin_recovery(unsigned core) {
        Guard g(gate_);
        if (!g.ok || !domain_ || core != 0 || recovering_) return false;
        recovering_ = true; quiet_ = 0; return true;
    }
    // The adapter attests producer, consumer, and DMA/other writer stopped.
    // These bits are declarations, NOT evidence that hardware is quiescent.
    bool attest(unsigned core, unsigned mask) {
        Guard g(gate_);
        if (!g.ok || core != 0 || !recovering_ || !mask || mask > 7) return false;
        quiet_ |= uint8_t(mask); return true;
    }
    bool restart(unsigned core) {
        Guard g(gate_);
        if (!g.ok || core != 0 || !recovering_ || quiet_ != 7 || epoch_ == Counter(-1)) return false;
        ++epoch_; head_ = size_ = ready_head_ = ready_size_ = notices_ = quiet_ = 0;
        for (auto& b : blocks_) b.phase = Phase::Free;
        recovering_ = false;
        return true;
    }
    // Serial observer only. Not a concurrent snapshot API.
    template<class Observer> void observe(Observer& o) const {
        o.start(domain_, epoch_, generation_, recovering_, quiet_, notices_, size_, ready_size_);
        for (unsigned n = 0; n < size_; ++n) o.message(messages_[(head_ + n) & 3]);
        o.blocks();
        for (const auto& b : blocks_) {
            uint32_t hash = 2166136261u;
            for (unsigned i = 0; i < b.written; ++i) hash = (hash ^ b.bytes[i]) * 16777619u;
            o.block(unsigned(b.phase), b.owner, b.generation, b.written, hash);
        }
        o.end();
    }
};
} // namespace h417_experiment
