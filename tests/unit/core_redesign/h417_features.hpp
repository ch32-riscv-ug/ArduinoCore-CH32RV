#pragma once
#include <stdint.h>

// Experimental feature storage. No MCU gate, startup, or scheduling policy.
namespace h417_feature_experiment {
enum class Owners : uint8_t { Single, Fixed, Shared };
enum class Phase : uint8_t { Free, Writing, Ready, Reading };
struct Message { uint32_t words[4]; };
struct Token { uint32_t domain, epoch, generation, index; };
namespace detail {
template<bool Enabled> struct Messages {};
template<> struct Messages<true> {
    Message messages_[4] = {};
    uint8_t head_ = 0, size_ = 0;
};
template<class Counter> struct Block {
    uint8_t bytes[64] = {};
    Counter generation = 0;
    Phase phase = Phase::Free;
    uint8_t owner = 0, written = 0;
};
template<bool Enabled, class Counter> struct Blocks {};
template<class Counter> struct Blocks<true, Counter> {
    Counter generation_ = 0;
    Block<Counter> blocks_[2] = {};
    uint8_t ready_[2] = {}, ready_head_ = 0, ready_size_ = 0;
};
} // namespace detail

template<Owners Policy, class Gate, bool MessagesEnabled, bool BlocksEnabled, class Counter = uint32_t>
class Fabric : private detail::Messages<MessagesEnabled>, private detail::Blocks<BlocksEnabled, Counter> {
    static_assert(Counter(-1) > Counter(0), "unsigned counter");
    using Block = detail::Block<Counter>;
    struct Guard {
        Gate& gate;
        bool ok;
        explicit Guard(Gate& g) : gate(g), ok(g.try_enter()) {}
        ~Guard() { if (ok) gate.leave(); }
    };
    Gate& gate_;
    const uint32_t domain_;
    Counter epoch_ = 1;
    uint8_t notices_ = 0, quiet_ = 0;
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
        if constexpr (!BlocksEnabled) return nullptr;
        else {
            if (t.domain != domain_ || t.epoch != epoch_ || t.index >= 2 || !t.generation) return nullptr;
            Block& b = this->blocks_[t.index];
            return b.phase != Phase::Free && b.generation == t.generation ? &b : nullptr;
        }
    }
public:
    constexpr explicit Fabric(Gate& gate, uint32_t domain) : gate_(gate), domain_(domain) {}
    Fabric(const Fabric&) = delete;
    Fabric& operator=(const Fabric&) = delete;
    Fabric(Fabric&&) = delete;
    Fabric& operator=(Fabric&&) = delete;
    bool push(unsigned core, const Message& m) {
        if constexpr (!MessagesEnabled) return false;
        else {
            Guard g(gate_);
            if (!g.ok || !active() || !producer(core) || this->size_ == 4) return false;
            this->messages_[(this->head_ + this->size_) & 3] = m;
            ++this->size_; notices_ |= 1;
            return true;
        }
    }
    bool pop(unsigned core, Message& out) {
        if constexpr (!MessagesEnabled) return false;
        else {
            Guard g(gate_);
            if (!g.ok || !active() || !consumer(core) || !this->size_) return false;
            out = this->messages_[this->head_]; this->head_ = (this->head_ + 1) & 3; --this->size_;
            return true;
        }
    }
    bool reserve(unsigned core, Token& out) {
        if constexpr (!BlocksEnabled) return false;
        else {
            Guard g(gate_);
            if (!g.ok || !active() || !producer(core) || this->generation_ == Counter(-1)) return false;
            for (unsigned i = 0; i < 2; ++i) {
                Block& b = this->blocks_[i];
                if (b.phase != Phase::Free) continue;
                b.phase = Phase::Writing; b.owner = uint8_t(core); b.written = 0;
                b.generation = ++this->generation_;
                out = Token{domain_, epoch_, b.generation, i};
                return true;
            }
            return false;
        }
    }
    bool write(unsigned core, Token t, unsigned index, unsigned value) {
        if constexpr (!BlocksEnabled) return false;
        else {
            Guard g(gate_);
            if (!g.ok || !active() || !producer(core)) return false;
            Block* b = find(t);
            if (!b || b->phase != Phase::Writing || b->owner != core ||
                index >= 64 || index != b->written || value > 255) return false;
            b->bytes[index] = uint8_t(value); ++b->written;
            return true;
        }
    }
    bool publish(unsigned core, Token t) {
        if constexpr (!BlocksEnabled) return false;
        else {
            Guard g(gate_);
            if (!g.ok || !active() || !producer(core)) return false;
            Block* b = find(t);
            if (!b || b->phase != Phase::Writing || b->owner != core || b->written != 64) return false;
            b->phase = Phase::Ready;
            this->ready_[(this->ready_head_ + this->ready_size_) & 1] = uint8_t(t.index);
            ++this->ready_size_; notices_ |= 2;
            return true;
        }
    }
    bool take(unsigned core, Token& out) {
        if constexpr (!BlocksEnabled) return false;
        else {
            Guard g(gate_);
            if (!g.ok || !active() || !consumer(core) || !this->ready_size_) return false;
            unsigned i = this->ready_[this->ready_head_]; this->ready_head_ ^= 1; --this->ready_size_;
            Block& b = this->blocks_[i]; b.phase = Phase::Reading; b.owner = uint8_t(core);
            out = Token{domain_, epoch_, b.generation, i};
            return true;
        }
    }
    bool read(unsigned core, Token t, unsigned index, unsigned& out) {
        if constexpr (!BlocksEnabled) return false;
        else {
            Guard g(gate_);
            if (!g.ok || !active() || !consumer(core)) return false;
            Block* b = find(t);
            if (!b || b->phase != Phase::Reading || b->owner != core || index >= 64) return false;
            out = b->bytes[index]; return true;
        }
    }
    bool release(unsigned core, Token t) {
        if constexpr (!BlocksEnabled) return false;
        else {
            Guard g(gate_);
            if (!g.ok || !active() || !consumer(core)) return false;
            Block* b = find(t);
            if (!b || b->phase != Phase::Reading || b->owner != core) return false;
            b->phase = Phase::Free; return true;
        }
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
        ++epoch_; notices_ = quiet_ = 0;
        if constexpr (MessagesEnabled) this->head_ = this->size_ = 0;
        if constexpr (BlocksEnabled) {
            this->ready_head_ = this->ready_size_ = 0;
            for (auto& b : this->blocks_) b.phase = Phase::Free;
        }
        recovering_ = false;
        return true;
    }
    // Serial observer only. Not a concurrent snapshot API.
    template<class Observer> void observe(Observer& o) const {
        unsigned generation = 0, messages = 0, ready = 0;
        if constexpr (MessagesEnabled) messages = this->size_;
        if constexpr (BlocksEnabled) { generation = this->generation_; ready = this->ready_size_; }
        o.start(domain_, epoch_, generation, recovering_, quiet_, notices_, messages, ready);
        if constexpr (MessagesEnabled)
            for (unsigned n = 0; n < this->size_; ++n) o.message(this->messages_[(this->head_ + n) & 3]);
        o.blocks();
        if constexpr (BlocksEnabled) for (const auto& b : this->blocks_) {
            uint32_t hash = 2166136261u;
            for (unsigned i = 0; i < b.written; ++i) hash = (hash ^ b.bytes[i]) * 16777619u;
            o.block(unsigned(b.phase), b.owner, b.generation, b.written, hash);
        }
        o.end();
    }
};
} // namespace h417_feature_experiment
