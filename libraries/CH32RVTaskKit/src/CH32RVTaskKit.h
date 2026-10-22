/* CH32RVTaskKit: short pieces of work that run when something says so - an
 * interrupt, a deadline, a period, a condition - instead of being called on
 * every loop(). Optional: a sketch that calls its steps from loop() does not
 * need it, and does not pay for it.
 *
 *     CH32RVTaskKit::Executor app;
 *     CH32RVTaskKit::Work blink(blinkStep);           // a function that returns quickly
 *     CH32RVSyncKit::Period every250(250);
 *
 *     void setup() { app.attach(blink); blink.wakeEvery(every250); }
 *     void loop()  { app.runOnce(); }                  // the one place work runs
 *
 * What this is and is not:
 *
 *   - A Work is a function plus "should it run now?". It keeps no stack and
 *     cannot wait in the middle: it does a bit, keeps its state in variables,
 *     and returns. That is the difference from ESP32TaskKit and FreeRTOS tasks,
 *     which can block because each has a stack of its own.
 *   - Work runs only inside runOnce(), which the sketch calls. Never from an
 *     interrupt, never from delay(): runOnce() is the visible place.
 *   - runOnce() looks at the triggers, then runs each ready Work once, in the
 *     order they were attached. A Work posted while the round runs (by itself,
 *     another Work or an ISR) runs in the next round, not twice in this one.
 *   - runOnce() inside a Work (of this or any executor) does nothing and
 *     returns 0: no nesting.
 *   - No time promise. A Work that does not return stops everything.
 *
 * No heap and no capacity: each Work is its own list node. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <CH32RVSyncKit.h>

extern "C" unsigned long micros(void);

namespace CH32RVTaskKit {

class Executor;

/* Optional numbers about one Work, collected only when attached with
 * Work::measure(). The times include any interrupt that ran during the Work;
 * they are what loop() waited, not CPU time, and the maximum is what was seen,
 * not a bound. */
struct WorkStats {
    uint32_t runs = 0;
    uint32_t totalUs = 0;     /* wraps after ~71 minutes of total run time */
    uint32_t maxUs = 0;

    void clear()
    {
        runs = 0;
        totalUs = 0;
        maxUs = 0;
    }
};

class Work {
public:
    explicit Work(void (*fn)()) : _plain(fn) {}
    Work(void (*fn)(void *), void *context) : _withContext(fn), _context(context) {}
    Work(const Work &) = delete;
    Work &operator=(const Work &) = delete;

    /* Ask for one run in the next round. From anywhere - loop(), another Work,
     * an ISR. Several posts before the run are one run. */
    void post()
    {
        CH32RVSyncKit::CriticalSection cs;
        _ready = true;
    }

    bool ready() const
    {
        CH32RVSyncKit::CriticalSection cs;
        return _ready;
    }

    /* Triggers the executor checks at the start of each round. Each Work has
     * one of each; setting one again replaces it, nullptr removes it. They only
     * post - the Work itself still decides what to do (and re-arms a deadline). */
    void wakeOn(const CH32RVSyncKit::Deadline *deadline) { _deadline = deadline; }
    void wakeOn(const CH32RVSyncKit::Deadline &deadline) { _deadline = &deadline; }
    void wakeEvery(CH32RVSyncKit::Period *period) { _period = period; }
    void wakeEvery(CH32RVSyncKit::Period &period) { _period = &period; }
    void wakeWhen(bool (*condition)()) { _when = condition; }

    /* Collect run counts and times into stats (nullptr stops). */
    void measure(WorkStats *stats) { _stats = stats; }
    void measure(WorkStats &stats) { _stats = &stats; }

    bool attached() const { return _owner != nullptr; }

private:
    friend class Executor;

    void call()
    {
        if (_plain) {
            _plain();
        } else {
            _withContext(_context);
        }
    }

    void (*_plain)() = nullptr;
    void (*_withContext)(void *) = nullptr;
    void *_context = nullptr;
    const CH32RVSyncKit::Deadline *_deadline = nullptr;
    CH32RVSyncKit::Period *_period = nullptr;
    bool (*_when)() = nullptr;
    WorkStats *_stats = nullptr;
    Work *_next = nullptr;
    Executor *_owner = nullptr;
    volatile bool _ready = false;
};

/* Optional numbers about the executor's rounds. */
struct RoundStats {
    uint32_t rounds = 0;
    uint32_t emptyRounds = 0;   /* rounds with nothing ready - not idle CPU time:
                                   loop() may be busy elsewhere */
    uint32_t maxUs = 0;         /* the longest round seen */

    void clear()
    {
        rounds = 0;
        emptyRounds = 0;
        maxUs = 0;
    }
};

class Executor {
public:
    Executor() = default;
    Executor(const Executor &) = delete;
    Executor &operator=(const Executor &) = delete;

    /* Add a Work at the end of the round order. It starts ready, so its first
     * run is the next runOnce() - never inside attach(). False when it belongs
     * to another executor; attaching it here again does nothing (true). */
    bool attach(Work &work)
    {
        if (work._owner == this) {
            return true;
        }
        if (work._owner) {
            return false;
        }
        work._next = nullptr;
        work._owner = this;
        Work **at = &_head;
        while (*at) {
            at = &(*at)->_next;
        }
        *at = &work;
        work.post();
        return true;
    }

    /* Take a Work out of the round. From inside a round (a Work that ends
     * itself) too: the round skips it and carries on. */
    void detach(Work &work)
    {
        if (work._owner != this) {
            return;
        }
        work._owner = nullptr;
        for (Work **at = &_head; *at; at = &(*at)->_next) {
            if (*at == &work) {
                *at = work._next;
                return;
            }
        }
    }

    /* One round: triggers first, then every Work that is ready, once each.
     * Returns how many ran; 0 when called from inside a Work. */
    size_t runOnce()
    {
        if (running()) {
            return 0;
        }
        running() = true;
        const uint32_t t0 = _stats ? (uint32_t)micros() : 0u;
        for (Work *w = _head; w; w = w->_next) {
            bool wake = w->_deadline && w->_deadline->expired();
            if (w->_period && w->_period->due()) {
                wake = true;
            }
            if (w->_when && w->_when()) {
                wake = true;
            }
            if (wake) {
                w->post();
            }
        }
        size_t ran = 0;
        for (Work *w = _head; w;) {
            Work *const next = w->_next;
            bool take;
            {
                CH32RVSyncKit::CriticalSection cs;
                take = w->_ready && w->_owner == this;
                if (take) {
                    w->_ready = false;     /* a post from now on is the next round's */
                }
            }
            if (take) {
                if (WorkStats *s = w->_stats) {
                    const uint32_t s0 = (uint32_t)micros();
                    w->call();
                    const uint32_t d = (uint32_t)micros() - s0;
                    s->runs++;
                    s->totalUs += d;
                    if (d > s->maxUs) {
                        s->maxUs = d;
                    }
                } else {
                    w->call();
                }
                ran++;
            }
            w = next;
        }
        if (_stats) {
            const uint32_t d = (uint32_t)micros() - t0;
            _stats->rounds++;
            if (!ran) {
                _stats->emptyRounds++;
            }
            if (d > _stats->maxUs) {
                _stats->maxUs = d;
            }
        }
        running() = false;
        return ran;
    }

    /* Something is ready now (triggers are not looked at until runOnce()). */
    bool anyReady() const
    {
        for (const Work *w = _head; w; w = w->_next) {
            if (w->ready()) {
                return true;
            }
        }
        return false;
    }

    void measure(RoundStats *stats) { _stats = stats; }
    void measure(RoundStats &stats) { _stats = &stats; }

private:
    /* One flag for every executor: a round inside another executor's Work is
     * as much nesting as one inside its own. */
    static bool &running()
    {
        static bool flag;
        return flag;
    }

    Work *_head = nullptr;
    RoundStats *_stats = nullptr;
};

}  // namespace CH32RVTaskKit
