#pragma once
#include <stdint.h>

// EXP-02 comparison prototypes. Serial-context operations only: an ISR or
// multicore caller must supply a separately verified synchronization policy.
// No dispatch, blocking wait, allocation, or callback invocation is performed.
namespace experiment {

struct NoWake { void ready() {} };
struct WakeFlag {
    bool* pending = nullptr;
    void ready() { if (pending) *pending = true; }
};

template<class T, unsigned N, class Wake = NoWake>
struct Queue : private Wake {
    static_assert(N > 0 && N <= 65535, "bounded capacity");
    static_assert(__is_trivially_copyable(T), "nontrivial object lifetimes are a separate comparison");
    T storage[N]{};
    uint16_t head = 0, count = 0;
    constexpr explicit Queue(Wake wake = {}) : Wake(wake) {}
    bool push(const T& value) {
        if (count == N) return false;
        storage[(head + count) % N] = value;
        if (++count == 1) Wake::ready();
        return true;
    }
    bool pop(T& value) {
        if (!count) return false;
        value = storage[head]; head = uint16_t((head + 1) % N); --count;
        return true;
    }
    unsigned size() const { return count; }
};

template<class T, class Wake = NoWake>
struct QueueView : private Wake {
    static_assert(__is_trivially_copyable(T), "trivial payload required");
    T* storage;
    uint16_t capacity, head = 0, count = 0;
    constexpr QueueView(T* data, unsigned n, Wake wake = {}) : Wake(wake), storage(data),
        capacity(data && n > 0 && n <= 65535 ? uint16_t(n) : 0) {}
    bool valid() const { return capacity != 0; }
    bool push(const T& value) {
        if (count == capacity) return false;
        storage[(head + count) % capacity] = value;
        if (++count == 1) Wake::ready();
        return true;
    }
    bool pop(T& value) {
        if (!count) return false;
        value = storage[head]; head = uint16_t((head + 1) % capacity); --count;
        return true;
    }
    unsigned size() const { return count; }
};

template<unsigned N>
struct ByteRing {
    static_assert(N > 0 && N <= 65535, "bounded capacity");
    struct Span { uint8_t* data; unsigned size; };
    uint8_t storage[N]{};
    uint16_t head = 0, count = 0, write_loan = 0, read_loan = 0;
    unsigned size() const { return count; }
    unsigned free() const { return N - count; }
    Span reserve() {
        if (write_loan || count == N) return {nullptr, 0};
        const unsigned tail = (head + count) % N;
        write_loan = uint16_t(free() < N - tail ? free() : N - tail);
        return {storage + tail, write_loan};
    }
    bool commit(unsigned n) {
        if (!write_loan || n > write_loan) return false;
        count = uint16_t(count + n); write_loan = 0;
        return true;
    }
    Span peek() {
        if (read_loan || !count) return {nullptr, 0};
        read_loan = uint16_t(count < N - head ? count : N - head);
        return {storage + head, read_loan};
    }
    bool consume(unsigned n) {
        if (!read_loan || n > read_loan) return false;
        head = uint16_t((head + n) % N); count = uint16_t(count - n); read_loan = 0;
        return true;
    }
    bool at(unsigned offset, uint8_t& value) const {
        if (offset >= count) return false;
        value = storage[(head + offset) % N]; return true;
    }
    unsigned write(const uint8_t* input, unsigned length) {
        if (write_loan) return 0;
        unsigned done = 0;
        while (done < length) {
            auto span = reserve();
            if (!span.size) break;
            const unsigned n = span.size < length - done ? span.size : length - done;
            for (unsigned i = 0; i < n; ++i) span.data[i] = input[done + i];
            commit(n); done += n;
        }
        return done;
    }
    unsigned read(uint8_t* output, unsigned length) {
        if (read_loan) return 0;
        unsigned done = 0;
        while (done < length) {
            auto span = peek();
            if (!span.size) break;
            const unsigned n = span.size < length - done ? span.size : length - done;
            for (unsigned i = 0; i < n; ++i) output[done + i] = span.data[i];
            consume(n); done += n;
        }
        return done;
    }
};

template<unsigned N, unsigned Max>
class MessageBuffer {
    static_assert(N >= 2 && Max <= 65535, "message limits");
    ByteRing<N> bytes;
public:
    unsigned size() const { return bytes.size(); }
    bool push(const uint8_t* data, unsigned length) {
        if (length > Max || length + 2 > bytes.free()) return false;
        const uint8_t header[2] = {uint8_t(length), uint8_t(length >> 8)};
        bytes.write(header, 2); bytes.write(data, length);
        return true;
    }
    bool pop(uint8_t* data, unsigned capacity, unsigned& length) {
        uint8_t low = 0, high = 0;
        if (!bytes.at(0, low) || !bytes.at(1, high)) return false;
        const unsigned n = unsigned(low) | unsigned(high) << 8;
        if (n > capacity || n > Max || n + 2 > bytes.size()) return false;
        uint8_t header[2]; bytes.read(header, 2); bytes.read(data, n);
        length = n; return true;
    }
};

template<unsigned Limit>
struct Semaphore {
    static_assert(Limit > 0 && Limit <= 65535, "semaphore limit");
    uint16_t count;
    bool valid;
    constexpr explicit Semaphore(unsigned initial = 0) : count(initial <= Limit ? uint16_t(initial) : 0),
        valid(initial <= Limit) {}
    bool acquire() { if (!valid || !count) return false; --count; return true; }
    bool release() { if (!valid || count == Limit) return false; ++count; return true; }
};

struct EventBits {
    uint32_t bits = 0;
    void set(uint32_t mask) { bits |= mask; }
    void clear(uint32_t mask) { bits &= ~mask; }
    bool take(uint32_t mask, bool all, bool consume, uint32_t& observed) {
        const uint32_t matched = bits & mask;
        if (!mask || (all ? matched != mask : matched == 0)) return false;
        observed = matched;
        if (consume) bits &= ~matched;
        return true;
    }
};

struct TryLock {
    uint32_t owner = 0;
    bool acquire(uint32_t token) {
        if (!token || owner) return false;
        owner = token; return true;
    }
    bool release(uint32_t token) {
        if (!token || owner != token) return false;
        owner = 0; return true;
    }
};

template<unsigned Block, unsigned N, class Generation = uint32_t>
struct Pool {
    static_assert(Block > 0 && N > 0 && N <= 32, "pool bounds");
    static_assert(Generation(-1) > Generation(0), "unsigned generation required");
    struct Token { unsigned index; Generation generation; };
    struct alignas(uint32_t) BlockStorage { uint8_t bytes[Block]; };
    BlockStorage storage[N]{};
    Generation generations[N]{};
    uint32_t used = 0;
    bool allocate(Token& out) {
        for (unsigned i = 0; i < N; ++i) {
            if ((used & (uint32_t(1) << i)) || generations[i] == Generation(~Generation(0))) continue;
            used |= uint32_t(1) << i; ++generations[i]; out = {i, generations[i]};
            return true;
        }
        return false;
    }
    uint8_t* data(Token token) {
        if (token.index >= N || !(used & (uint32_t(1) << token.index)) ||
            generations[token.index] != token.generation) return nullptr;
        return storage[token.index].bytes;
    }
    bool release(Token token) {
        if (!data(token)) return false;
        used &= ~(uint32_t(1) << token.index); return true;
    }
};

} // namespace experiment
