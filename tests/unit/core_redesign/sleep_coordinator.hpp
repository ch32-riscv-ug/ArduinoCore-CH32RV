#pragma once
#include "io_lifecycle.hpp"
#include "logical_time.hpp"

// Serial fake adapters. No WFI/WFE, real clock restoration, ISR synchronization,
// or implicit user callbacks. Instance lifetimes include all leases/recovery.
namespace sleep_experiment {
enum class Action : uint8_t { Prepare, Quiet, Off, Restore, Activate, Abort };
enum class Reply : uint8_t { Waiting, Ready, Failed };
enum class Phase : uint8_t { Unbound, Active, Quiescing, Armed, Sleeping, Recovering, Fault };
enum class Entry : uint8_t { Rejected, Unsupported, Sleeping, Woke, Fault };
enum class Injection : uint8_t { None, BeforeCheck, AfterOff, AfterWait };
using Callback = Reply (*)(void*, Action);
using Clock = time_experiment::LogicalClock<32, false, time_experiment::SixMHz>;
using Timer = time_experiment::DomainTimer<>;
struct NoDeadline {};

template<class Deadline> class DeadlineBinding {
    Deadline* deadlines = nullptr;
protected:
    void attach(Deadline& source) { deadlines = &source; }
    bool deadline_pending() const { return deadlines->pending(); }
};
// Empty base: no placeholder object, pointer, table or runtime branch required.
template<> class DeadlineBinding<NoDeadline> {
protected:
    bool deadline_pending() const { return false; }
};

template<unsigned Bytes = 32> struct Port {
    experiment::IoBuffer<Bytes> buffer;
    experiment::IoSlot<Bytes> io;
    bool powered = false, admitting = false;
    Reply restoration = Reply::Waiting;
    bool init() {
        if (powered || !io.open()) return false;
        powered = admitting = true;
        return true;
    }
    experiment::IoStart start(unsigned length) {
        if (!powered || !admitting) return experiment::IoStart::Closed;
        return io.start(&buffer, length, experiment::IoMode::Read, 0x3c);
    }
    bool finish(uint32_t token, experiment::IoResult result, unsigned count,
                uint8_t fill, bool quiescent) {
        return quiescent && powered && io.hardware_terminal(token, result, count, fill);
    }
    Reply step(Action action) {
        switch (action) {
        case Action::Prepare:
            admitting = false;
            if (io.active() && !io.terminal()) io.cancel(io.token());
            return io.active() ? Reply::Waiting : Reply::Ready;
        case Action::Quiet:
            return io.active() ? Reply::Waiting : Reply::Ready;
        case Action::Off:
            if (io.active()) return Reply::Failed;
            admitting = false;
            io.end();
            powered = false;
            restoration = Reply::Waiting;
            return Reply::Ready;
        case Action::Restore:
            if (powered) return Reply::Ready;
            if (restoration != Reply::Ready) return restoration;
            if (!io.open()) return Reply::Failed;
            powered = true;
            return Reply::Ready;
        case Action::Activate:
            if (!powered || !io.opened() || io.active()) return Reply::Failed;
            admitting = true;
            return Reply::Ready;
        case Action::Abort:
            if (!powered || !io.opened()) return Reply::Failed;
            admitting = true; // an already-requested cancellation stays requested
            return Reply::Ready;
        }
        return Reply::Failed;
    }
    static Reply callback(void* context, Action action) {
        return static_cast<Port*>(context)->step(action);
    }
};

inline Reply combine(Reply a, Reply b) {
    if (a == Reply::Failed || b == Reply::Failed) return Reply::Failed;
    return a == Reply::Waiting || b == Reply::Waiting ? Reply::Waiting : Reply::Ready;
}

template<class A, class B> class Explicit {
    A* first = nullptr;
    B* second = nullptr;
    bool locked = false;
public:
    Explicit() = default;
    Explicit(const Explicit&) = delete;
    Explicit& operator=(const Explicit&) = delete;
    bool bind(A& a, B& b) {
        if (first || second || locked) return false;
        first = &a; second = &b;
        return true;
    }
    bool freeze() {
        if (locked || !first || !second) return false;
        locked = true; return true;
    }
    void thaw() { locked = false; }
    bool frozen() const { return locked; }
    unsigned count() const { return first && second ? 2 : 0; }
    bool contains(const void* context) const { return context && (context == first || context == second); }
    Reply visit(Action action) {
        if (!locked) return Reply::Failed;
        const Reply a = first->step(action);
        const Reply b = second->step(action);
        return combine(a, b);
    }
};

template<unsigned Capacity, class Generation = uint32_t> class Registry {
    static_assert(Capacity > 0 && Capacity <= 255, "bounded participant table");
    static_assert(std::is_unsigned<Generation>::value && !std::is_same<Generation, bool>::value,
                  "unsigned generation");
    struct Item { void* context = nullptr; Callback callback = nullptr; Generation generation = 0; };
    Item items[Capacity]{};
    Generation generation = 0;
    bool locked = false;
public:
    struct Token { Generation generation; uint8_t index; };
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    bool add(void* context, Callback callback, Token& out) {
        if (locked || !context || !callback || generation == std::numeric_limits<Generation>::max()) return false;
        unsigned free = Capacity;
        for (unsigned i = 0; i < Capacity; ++i) {
            if (items[i].context == context) return false;
            if (!items[i].context && free == Capacity) free = i;
        }
        if (free == Capacity) return false;
        items[free] = Item{context, callback, ++generation};
        out = Token{generation, uint8_t(free)};
        return true;
    }
    bool remove(Token token) {
        if (locked || token.index >= Capacity || !token.generation) return false;
        Item& item = items[token.index];
        if (!item.context || item.generation != token.generation ||
            item.callback(item.context, Action::Quiet) != Reply::Ready) return false;
        item = Item{};
        return true;
    }
    bool freeze() { if (locked) return false; locked = true; return true; }
    void thaw() { locked = false; }
    bool frozen() const { return locked; }
    unsigned count() const {
        unsigned n = 0;
        for (const auto& item : items) if (item.context) ++n;
        return n;
    }
    bool contains(const void* context) const {
        if (!context) return false;
        for (const auto& item : items) if (item.context == context) return true;
        return false;
    }
    Reply visit(Action action) {
        if (!locked) return Reply::Failed;
        Reply reply = Reply::Ready;
        for (auto& item : items)
            if (item.context) reply = combine(reply, item.callback(item.context, action));
        return reply;
    }
};

template<class Participants, class Generation = uint32_t, class Deadline = Timer>
class Coordinator : private DeadlineBinding<Deadline> {
    static_assert(std::is_unsigned<Generation>::value && !std::is_same<Generation, bool>::value,
                  "unsigned generation");
    Participants* participants = nullptr;
    Clock* clock = nullptr;
    Generation generation = 0;
    Phase state = Phase::Unbound;
    bool input = false, time_restored = true;
    bool pending() const { return input || this->deadline_pending(); }
    bool bind_common(Participants& list, Clock& source) {
        if (state != Phase::Unbound || source.phase() != time_experiment::State::Running) return false;
        participants = &list; clock = &source; state = Phase::Active;
        return true;
    }
    void abort_prepare() {
        if (participants->visit(Action::Abort) != Reply::Ready) {
            participants->visit(Action::Prepare); state = Phase::Fault; return;
        }
        participants->thaw();
        state = Phase::Active;
    }
public:
    Coordinator() = default;
    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;
    bool bind(Participants& list, Clock& source, Deadline& deadlines) {
        if (!bind_common(list, source)) return false;
        this->attach(deadlines);
        return true;
    }
    bool bind(Participants& list, Clock& source) {
        static_assert(std::is_same<Deadline, NoDeadline>::value, "timer dependency requires a source");
        return bind_common(list, source);
    }
    bool request(Generation& out) {
        if (state != Phase::Active || clock->phase() != time_experiment::State::Running ||
            pending() || generation == std::numeric_limits<Generation>::max()) return false;
        if (!participants->freeze()) return false;
        ++generation; out = generation;
        state = Phase::Quiescing;
        if (participants->visit(Action::Prepare) == Reply::Failed) state = Phase::Fault;
        return true;
    }
    void post_input() {
        input = true;
        if (state == Phase::Sleeping) state = Phase::Recovering;
    }
    bool take_input() { const bool was = input; input = false; return was; }
    bool abort() {
        if (state != Phase::Quiescing && state != Phase::Armed) return false;
        abort_prepare(); return state == Phase::Active;
    }
    Phase poll() {
        if (state == Phase::Quiescing || state == Phase::Armed) {
            if (pending()) abort_prepare();
            else {
                const Reply reply = participants->visit(Action::Prepare);
                if (reply == Reply::Failed) state = Phase::Fault;
                else state = reply == Reply::Ready ? Phase::Armed : Phase::Quiescing;
            }
        } else if (state == Phase::Recovering && clock_ready()) {
            const Reply reply = participants->visit(Action::Restore);
            if (reply == Reply::Failed) state = Phase::Fault;
            else if (reply == Reply::Ready) {
                if (participants->visit(Action::Activate) != Reply::Ready) {
                    participants->visit(Action::Prepare); state = Phase::Fault;
                }
                else { participants->thaw(); state = Phase::Active; }
            }
        }
        return state;
    }
    Entry enter(Generation token, bool wake_armed, bool atomic_gate_supported, Injection injection) {
        if (state != Phase::Armed || !token || token != generation) return Entry::Rejected;
        if (injection == Injection::BeforeCheck) post_input();
        if (pending()) { abort_prepare(); return Entry::Rejected; }
        if (!wake_armed || !atomic_gate_supported) return Entry::Unsupported;
        const Reply quiet = participants->visit(Action::Quiet);
        if (quiet == Reply::Failed) { state = Phase::Fault; return Entry::Fault; }
        if (quiet == Reply::Waiting) { state = Phase::Quiescing; return Entry::Rejected; }
        if (!clock->pause()) { state = Phase::Fault; return Entry::Fault; }
        time_restored = false;
        if (participants->visit(Action::Off) != Reply::Ready) { state = Phase::Fault; return Entry::Fault; }
        state = Phase::Sleeping;
        // A fake atomic gate latches work at these boundaries. This is an
        // assumed adapter contract, not a tested instruction sequence.
        if (injection == Injection::AfterOff || injection == Injection::AfterWait) post_input();
        return state == Phase::Sleeping ? Entry::Sleeping : Entry::Woke;
    }
    bool wake(Generation token) {
        if (state != Phase::Sleeping || !token || token != generation) return false;
        state = Phase::Recovering; return true; // may be timer or spurious wake
    }
    time_experiment::Result restore_clock(Generation token, const time_experiment::Sample& sample,
                                        bool elapsed_known, uint64_t elapsed, uint64_t anchor,
                                        time_experiment::Stamp& out) {
        using namespace time_experiment;
        if ((state != Phase::Recovering && state != Phase::Fault) || !token || token != generation || clock_ready())
            return Result::Closed;
        Dropped drop{};
        const Result result = elapsed_known ? clock->resume_known(sample, elapsed, false, out, drop)
                                            : clock->resume_unknown(sample, anchor, out);
        if (ready(result)) time_restored = true;
        return result;
    }
    bool retry() {
        if (state != Phase::Fault) return false;
        state = Phase::Recovering; return true;
    }
    Phase phase() const { return state; }
    Generation attempt() const { return generation; }
    bool input_pending() const { return input; }
    bool clock_ready() const {
        return time_restored && clock && clock->phase() == time_experiment::State::Running;
    }
};
} // namespace sleep_experiment
