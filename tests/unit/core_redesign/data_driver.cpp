#include "data_parts.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

static uint32_t hash(const uint8_t* data, unsigned n) {
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ data[i]) * 16777619u;
    return h;
}

template<class Q>
int queue_driver(Q& queue, bool& wake) {
    char op[20]; int a; int32_t last = -1;
    while (std::scanf("%19s", op) == 1) {
        bool ok = false;
        if (!std::strcmp(op, "push")) { if (std::scanf("%d", &a) != 1) return 2; ok = queue.push(a); }
        else if (!std::strcmp(op, "pop")) ok = queue.pop(last);
        else if (!std::strcmp(op, "clear")) { wake = false; ok = true; }
        else return 2;
        std::printf("%d %u %d %d\n", ok, queue.size(), last, wake);
    }
    return 0;
}

template<unsigned N>
int ring_driver() {
    experiment::ByteRing<N> ring;
    uint8_t buffer[257]{};
    char line[80], op[20]; int n = 0, value = 0;
    typename experiment::ByteRing<N>::Span writing{nullptr, 0};
    while (std::fgets(line, sizeof(line), stdin)) {
        n = value = 0;
        if (std::sscanf(line, "%19s %d %d", op, &n, &value) < 1) continue;
        if (n < 0 || n > 257) return 2;
        unsigned result = 0; uint32_t digest = 0;
        if (!std::strcmp(op, "write")) {
            for (int i = 0; i < n; ++i) buffer[i] = uint8_t(value + i);
            result = ring.write(buffer, unsigned(n));
        } else if (!std::strcmp(op, "read")) {
            result = ring.read(buffer, unsigned(n)); digest = hash(buffer, result);
        } else if (!std::strcmp(op, "reserve")) { writing = ring.reserve(); result = writing.size; }
        else if (!std::strcmp(op, "fill")) {
            if (!writing.data) return 2;
            for (unsigned i = 0; i < writing.size; ++i) writing.data[i] = uint8_t(n);
            result = writing.size;
        } else if (!std::strcmp(op, "commit")) result = ring.commit(unsigned(n));
        else if (!std::strcmp(op, "peek")) {
            auto span = ring.peek(); result = span.size; digest = hash(span.data, span.size);
        } else if (!std::strcmp(op, "consume")) result = ring.consume(unsigned(n));
        else return 2;
        std::printf("%u %u %u\n", result, ring.size(), digest);
    }
    return 0;
}

template<unsigned N, unsigned Max>
int message_driver() {
    experiment::MessageBuffer<N, Max> messages;
    uint8_t buffer[129]{};
    char line[80], op[20]; int n = 0, value = 0;
    unsigned length = 999;
    while (std::fgets(line, sizeof(line), stdin)) {
        n = value = 0;
        if (std::sscanf(line, "%19s %d %d", op, &n, &value) < 1) continue;
        if (n < 0 || n > 128) return 2;
        bool ok = false; uint32_t digest = 0;
        if (!std::strcmp(op, "push")) {
            for (int i = 0; i < n; ++i) buffer[i] = uint8_t(value + i);
            ok = messages.push(buffer, unsigned(n));
        } else if (!std::strcmp(op, "pop")) {
            ok = messages.pop(buffer, unsigned(n), length);
            if (ok) digest = hash(buffer, length);
        } else return 2;
        std::printf("%d %u %u %u\n", ok, messages.size(), length, digest);
    }
    return 0;
}

static void sync_contracts() {
    struct Wide { uint32_t words[4]; };
    experiment::Queue<Wide, 1> wide;
    const Wide pattern{{0x11223344u, 0x55667788u, 0xaabbccddu, 0xfeedbeefu}};
    Wide copied{};
    assert(wide.push(pattern) && !wide.push({{0, 0, 0, 0}}) && wide.pop(copied));
    for (unsigned i = 0; i < 4; ++i) assert(copied.words[i] == pattern.words[i]);
    experiment::Queue<uint8_t, 4> narrow;
    for (unsigned i = 0; i < 4; ++i) assert(narrow.push(uint8_t(252 + i)));
    for (unsigned i = 0; i < 4; ++i) { uint8_t out = 0; assert(narrow.pop(out) && out == 252 + i); }
    experiment::Semaphore<4> semaphore;
    assert(!semaphore.acquire());
    for (unsigned i = 0; i < 4; ++i) assert(semaphore.release());
    assert(!semaphore.release());
    for (unsigned i = 0; i < 4; ++i) assert(semaphore.acquire());
    assert(!semaphore.acquire());
    experiment::Semaphore<1> invalid(2);
    assert(!invalid.valid && !invalid.acquire() && !invalid.release());
    experiment::EventBits flags;
    uint32_t result = 123;
    flags.set(1); assert(!flags.take(3, true, true, result) && flags.bits == 1 && result == 123);
    flags.set(2); assert(flags.take(3, true, false, result) && result == 3 && flags.bits == 3);
    assert(flags.take(1, false, true, result) && flags.bits == 2);
    assert(!flags.take(0, true, true, result));
    flags.clear(2); assert(flags.bits == 0);
    experiment::TryLock lock;
    assert(!lock.acquire(0) && lock.acquire(1));
    assert(!lock.acquire(1) && !lock.acquire(2) && !lock.release(2));
    assert(lock.release(1) && !lock.release(1) && lock.acquire(2));
    experiment::Pool<3, 4> pool;
    experiment::Pool<3, 4>::Token handles[4], extra;
    for (unsigned i = 0; i < 4; ++i) {
        assert(pool.allocate(handles[i]));
        assert(uintptr_t(pool.data(handles[i])) % alignof(uint32_t) == 0);
    }
    assert(!pool.allocate(extra));
    const auto stale = handles[2];
    assert(pool.release(stale) && !pool.release(stale));
    assert(pool.allocate(extra) && extra.index == stale.index && !pool.data(stale));
    assert(!pool.release(stale) && pool.release(extra));
    experiment::Pool<16, 1, uint8_t> tiny_generation;
    experiment::Pool<16, 1, uint8_t>::Token token;
    for (unsigned i = 0; i < 255; ++i) assert(tiny_generation.allocate(token) && tiny_generation.release(token));
    assert(!tiny_generation.allocate(token)); // Retired, not wrapped into an old token.
    experiment::QueueView<int32_t> invalid_view(nullptr, 4);
    int32_t value = 0;
    assert(!invalid_view.valid() && !invalid_view.push(1) && !invalid_view.pop(value));
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    bool wake = false;
    if (!std::strcmp(argv[1], "queue1")) {
        experiment::Queue<int32_t, 1, experiment::WakeFlag> q({&wake}); return queue_driver(q, wake);
    }
    if (!std::strcmp(argv[1], "queue4")) {
        experiment::Queue<int32_t, 4, experiment::WakeFlag> q({&wake}); return queue_driver(q, wake);
    }
    if (!std::strcmp(argv[1], "queue16")) {
        experiment::Queue<int32_t, 16, experiment::WakeFlag> q({&wake}); return queue_driver(q, wake);
    }
    if (!std::strcmp(argv[1], "view4")) {
        int32_t storage[4]{};
        experiment::QueueView<int32_t, experiment::WakeFlag> q(storage, 4, {&wake}); return queue_driver(q, wake);
    }
    if (!std::strcmp(argv[1], "ring16")) return ring_driver<16>();
    if (!std::strcmp(argv[1], "ring64")) return ring_driver<64>();
    if (!std::strcmp(argv[1], "ring256")) return ring_driver<256>();
    if (!std::strcmp(argv[1], "message32")) return message_driver<32, 8>();
    if (!std::strcmp(argv[1], "message128")) return message_driver<128, 32>();
    if (!std::strcmp(argv[1], "sync")) { sync_contracts(); std::puts("ok"); return 0; }
    return 2;
}
