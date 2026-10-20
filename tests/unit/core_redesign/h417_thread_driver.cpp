#include "h417_ownership.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace h417_experiment;
// Tickets measure lock acquisition order, not return/callback completion order.
struct Tracking {
    inline static thread_local uint64_t ticket = 0;
    uint64_t clock = 0; // Protected by the selected gate.
    std::atomic<unsigned> attempts{0}, busy{0};
    void entered() { ticket = ++clock; }
};
struct MutexGate : Tracking {
    std::mutex mutex;
    bool try_enter() {
        attempts.fetch_add(1, std::memory_order_relaxed);
        if (!mutex.try_lock()) { busy.fetch_add(1, std::memory_order_relaxed); return false; }
        entered(); return true;
    }
    void leave() { mutex.unlock(); }
};
struct AtomicGate : Tracking {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
    bool try_enter() {
        attempts.fetch_add(1, std::memory_order_relaxed);
        if (flag.test_and_set(std::memory_order_acquire)) {
            busy.fetch_add(1, std::memory_order_relaxed); return false;
        }
        entered(); return true;
    }
    void leave() { flag.clear(std::memory_order_release); }
};
template<class Function> void retry(Function f) {
    while (!f()) std::this_thread::yield(); // Caller-side host instrumentation.
}
struct Record { uint64_t ticket; unsigned producer, sequence; };
Message message(unsigned p, unsigned n) { return {{p, n, ~n, ((p+17)*65537u)^n}}; }
void verify(const Message& m) {
    assert(m.words[0] < 2);
    const auto expected = message(m.words[0], m.words[1]);
    for (unsigned i = 0; i < 4; ++i) assert(m.words[i] == expected.words[i]);
}
uint8_t pattern(unsigned p, unsigned n, unsigned i) {
    if (i == 0) return uint8_t(p);
    if (i <= 4) return uint8_t(n >> ((i-1)*8));
    if (i <= 8) return uint8_t(~n >> ((i-5)*8));
    return uint8_t(n*13+p*37+i*11);
}
Record verify(const std::array<uint8_t, 64>& bytes, uint64_t ticket) {
    unsigned p = bytes[0], n = 0;
    assert(p < 2);
    for (unsigned i = 1; i <= 4; ++i) n |= unsigned(bytes[i]) << ((i-1)*8);
    for (unsigned i = 0; i < 64; ++i) assert(bytes[i] == pattern(p, n, i));
    return {ticket, p, n};
}
struct EmptyObserver {
    void start(unsigned, unsigned, unsigned, bool recovery, unsigned quiet,
               unsigned, unsigned messages, unsigned ready) {
        assert(!recovery && !quiet && !messages && !ready);
    }
    void message(const Message&) { assert(false); }
    void blocks() {}
    void block(unsigned phase, unsigned, unsigned, unsigned, uint32_t) { assert(phase == 0); }
    void end() {}
};
unsigned validate(std::array<std::vector<Record>, 2>& records, unsigned producers, unsigned count) {
    std::vector<Record> ordered = records[0];
    ordered.insert(ordered.end(), records[1].begin(), records[1].end());
    assert(ordered.size() == producers*count);
    std::sort(ordered.begin(), ordered.end(), [](const Record& a, const Record& b) { return a.ticket < b.ticket; });
    std::array<unsigned, 2> next{};
    uint64_t previous = 0;
    for (auto r : ordered) {
        assert(r.ticket > previous && r.producer < producers && r.sequence == next[r.producer]++);
        previous = r.ticket;
    }
    for (unsigned p = 0; p < producers; ++p) assert(next[p] == count);
    return unsigned(ordered.size());
}

template<class Gate, Owners Policy> void stress(unsigned seed, unsigned messages, unsigned blocks) {
    Gate gate;
    Fabric<Policy, Gate> fabric(gate, 7);
    constexpr bool shared = Policy == Owners::Shared;
    constexpr unsigned producers = shared ? 2 : 1;
    std::atomic<bool> start{false};
    std::atomic<unsigned> message_done{0}, block_done{0};
    std::array<std::vector<Record>, 2> received_messages, received_blocks;
    std::array<std::thread, 2> threads;
    for (unsigned core = 0; core < 2; ++core) threads[core] = std::thread([&, core] {
        const bool produces = shared || core == 0, consumes = shared || core == 1;
        unsigned sent_message = 0, sent_block = 0, written = 0, read = 0;
        bool writing = false, reading = false;
        Token writer{}, reader{};
        uint64_t read_ticket = 0;
        std::array<uint8_t, 64> bytes{};
        uint32_t random = seed + 17*core + 1;
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (;;) {
            const bool produced = !produces || (sent_message == messages && sent_block == blocks && !writing);
            const bool consumed = !consumes || (message_done.load(std::memory_order_relaxed) == producers*messages &&
                                                block_done.load(std::memory_order_relaxed) == producers*blocks && !reading);
            if (produced && consumed) break;
            if (produces && sent_message < messages && fabric.push(core, message(core, sent_message))) ++sent_message;
            if (consumes) {
                Message m{};
                if (fabric.pop(core, m)) {
                    const auto ticket = Gate::ticket;
                    verify(m); received_messages[core].push_back({ticket, m.words[0], m.words[1]});
                    message_done.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (produces && !writing && sent_block < blocks && fabric.reserve(core, writer)) {
                writing = true; written = 0;
            }
            if (writing) {
                if (written < 64 && fabric.write(core, writer, written, pattern(core, sent_block, written))) ++written;
                if (written == 64 && fabric.publish(core, writer)) { writing = false; ++sent_block; }
            }
            if (consumes && !reading && fabric.take(core, reader)) {
                read_ticket = Gate::ticket; reading = true; read = 0;
            }
            if (reading) {
                unsigned byte = 999;
                if (read < 64 && fabric.read(core, reader, read, byte)) bytes[read++] = uint8_t(byte);
                if (read == 64 && fabric.release(core, reader)) {
                    received_blocks[core].push_back(verify(bytes, read_ticket));
                    reading = false; block_done.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (consumes) (void)fabric.ack(core, 3);
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            if ((random & 7) == 0) std::this_thread::yield();
        }
    });
    start.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();
    unsigned m = validate(received_messages, producers, messages), b = validate(received_blocks, producers, blocks);
    EmptyObserver observer; fabric.observe(observer); // Only after join.
    std::cout << "{\"messages\":" << m << ",\"blocks\":" << b
              << ",\"attempts\":" << gate.attempts << ",\"busy\":" << gate.busy << "}\n";
}

template<class Gate> void busy() {
    Gate gate;
    Fabric<Owners::Fixed, Gate> fabric(gate, 7);
    assert(gate.try_enter());
    std::thread caller([&] {
        Token t{9, 9, 9, 9}; Message m{{9, 9, 9, 9}}; unsigned byte = 999;
        assert(!fabric.reserve(0, t) && !fabric.push(0, m) && !fabric.pop(1, m));
        assert(!fabric.write(0, t, 0, 1) && !fabric.publish(0, t));
        assert(!fabric.take(1, t) && !fabric.read(1, t, 0, byte) && !fabric.release(1, t));
        assert(!fabric.ack(1, 3) && !fabric.begin_recovery(0) && !fabric.attest(0, 7) && !fabric.restart(0));
        assert(t.domain == 9 && t.epoch == 9 && t.generation == 9 && t.index == 9 && byte == 999);
        for (auto w : m.words) assert(w == 9);
    });
    caller.join(); gate.leave();
    Token t{}; assert(fabric.reserve(0, t));
    std::cout << "{\"rejected\":12,\"busy\":" << gate.busy << "}\n";
}

template<class Gate, Owners Policy> void recovery() {
    Gate gate;
    Fabric<Policy, Gate> fabric(gate, 7);
    Token old_writer{}, old_reader{};
    assert(fabric.reserve(0, old_reader));
    for (unsigned i = 0; i < 64; ++i) assert(fabric.write(0, old_reader, i, i));
    assert(fabric.publish(0, old_reader) && fabric.take(1, old_reader));
    assert(fabric.reserve(0, old_writer));
    std::atomic<unsigned> parked{0}, quiet{0}, stale_done{0};
    std::atomic<bool> stop{false}, restarted{false};
    std::array<std::thread, 2> workers;
    for (unsigned core = 0; core < 2; ++core) workers[core] = std::thread([&, core] {
        for (unsigned i = 0; i < 17; ++i) {
            if (core == 0) retry([&] { return fabric.write(0, old_writer, i, i); });
            else { unsigned out = 999; retry([&] { return fabric.read(1, old_reader, i, out); }); assert(out == i); }
        }
        parked.fetch_add(1, std::memory_order_release);
        while (!stop.load(std::memory_order_acquire)) std::this_thread::yield();
        if (core == 0) assert(!fabric.write(0, old_writer, 17, 99));
        else { unsigned out = 999; assert(!fabric.read(1, old_reader, 17, out) && out == 999); }
        quiet.fetch_or(1u << core, std::memory_order_release);
        while (!restarted.load(std::memory_order_acquire)) std::this_thread::yield();
        if (core == 0) assert(!fabric.write(0, old_writer, 17, 99) && !fabric.publish(0, old_writer));
        else assert(!fabric.release(1, old_reader));
        stale_done.fetch_add(1, std::memory_order_release);
    });
    while (parked.load(std::memory_order_acquire) != 2) std::this_thread::yield();
    retry([&] { return fabric.begin_recovery(0); });
    stop.store(true, std::memory_order_release);
    while (quiet.load(std::memory_order_acquire) != 3) std::this_thread::yield();
    assert(fabric.attest(0, 3) && !fabric.restart(0)); // Missing other/DMA writer attestation.
    assert(fabric.attest(0, 4) && fabric.restart(0));
    Token fresh{}; assert(fabric.reserve(0, fresh) && fresh.epoch == 2);
    restarted.store(true, std::memory_order_release);
    while (stale_done.load(std::memory_order_acquire) != 2) std::this_thread::yield();
    for (auto& t : workers) t.join();
    for (unsigned i = 0; i < 64; ++i) assert(fabric.write(0, fresh, i, 255-i));
    assert(fabric.publish(0, fresh) && fabric.take(1, fresh));
    for (unsigned i = 0; i < 64; ++i) { unsigned out = 999; assert(fabric.read(1, fresh, i, out) && out == 255-i); }
    assert(fabric.release(1, fresh));
    EmptyObserver observer; fabric.observe(observer);
    std::cout << "{\"epoch\":2,\"old_calls_rejected\":5,\"fresh_bytes\":64}\n";
}
template<class Gate> int select(const std::string& mode, unsigned seed, unsigned messages, unsigned blocks) {
    if (mode == "fixed") stress<Gate, Owners::Fixed>(seed, messages, blocks);
    else if (mode == "shared") stress<Gate, Owners::Shared>(seed, messages, blocks);
    else if (mode == "busy") busy<Gate>();
    else if (mode == "recovery-fixed") recovery<Gate, Owners::Fixed>();
    else if (mode == "recovery-shared") recovery<Gate, Owners::Shared>();
    else return 2;
    return 0;
}
int main(int argc, char** argv) {
    if (argc != 6) return 1;
    unsigned seed = std::strtoul(argv[3], nullptr, 10), messages = std::strtoul(argv[4], nullptr, 10), blocks = std::strtoul(argv[5], nullptr, 10);
    if (std::string(argv[1]) == "mutex") return select<MutexGate>(argv[2], seed, messages, blocks);
    if (std::string(argv[1]) == "atomic") return select<AtomicGate>(argv[2], seed, messages, blocks);
    return 3;
}
