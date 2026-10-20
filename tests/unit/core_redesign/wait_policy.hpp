#pragma once
#include "work_executor.hpp"
#include <stdint.h>

namespace experiment {
enum class WaitStatus { Completed, Pending, TimedOut, Forbidden, InvalidTimeout,
                        CannotProgress, ClockUnavailable, ProgressStopped, Idle };
enum class WaitProducer { Interrupt, System, User, OtherCore };
struct WaitOutcome { WaitStatus status; uint32_t elapsed; };
struct WaitPermissions {
    WorkOpportunity opportunity = WorkOpportunity::Explicit;
    WorkProfile profile = WorkProfile::M;
    bool user_opted = false, irq_enabled = false, remote_running = false, clock_running = false;
    bool supports(WaitProducer producer) const {
        if (producer == WaitProducer::Interrupt) return irq_enabled;
        if (producer == WaitProducer::OtherCore) return remote_running;
        if (producer != WaitProducer::System && producer != WaitProducer::User) return false;
        if (opportunity == WorkOpportunity::Explicit) return true;
        if (profile == WorkProfile::M) return false;
        return producer == WaitProducer::System || (profile == WorkProfile::U && user_opted);
    }
};

// Experiment only: loop-only synchronous wrapper. Attempt is an immediate try
// operation. Drive pumps ONLY the declared scope, returning false on a known
// inability to progress. It must return; this wrapper does not preempt it.
// Availability wins at each observation. Timeout is checked after a failed
// attempt; successful late observation is reported with elapsed > timeout.
template<class Attempt, class Drive, class Clock>
WaitOutcome wait_for(Attempt attempt, Drive drive, Clock clock, uint32_t timeout,
                     WorkContext context, WaitProducer producer, const WaitPermissions& permissions) {
    if (context != WorkContext::Normal) return {WaitStatus::Forbidden, 0};
    if (timeout >= 0x80000000u) return {WaitStatus::InvalidTimeout, 0};
    const uint32_t start = clock();
    if (attempt()) return {WaitStatus::Completed, uint32_t(clock() - start)};
    if (!timeout) return {WaitStatus::TimedOut, 0};
    if (!permissions.clock_running) return {WaitStatus::ClockUnavailable, 0};
    if (!permissions.supports(producer)) return {WaitStatus::CannotProgress, 0};
    for (;;) {
        const uint32_t elapsed = clock() - start;
        if (elapsed >= timeout) return {WaitStatus::TimedOut, elapsed};
        if (!drive()) return {WaitStatus::ProgressStopped, uint32_t(clock() - start)};
        if (attempt()) return {WaitStatus::Completed, uint32_t(clock() - start)};
    }
}

// Explicit stateful continuation: poll never pumps, sleeps or invokes a user
// handler. Ordinary callbacks may call it; ISR/setup are excluded in this
// serial-context experiment. Cancelled/completed waits cannot consume again.
class AsyncWait {
    uint32_t start = 0, timeout = 0;
    bool active = false;
public:
    bool arm(uint32_t now, uint32_t duration) {
        if (duration >= 0x80000000u) return false;
        start = now; timeout = duration; active = true; return true;
    }
    bool cancel() { const bool old = active; active = false; return old; }
    template<class Attempt> WaitOutcome poll(uint32_t now, Attempt attempt, WorkContext context = WorkContext::Normal) {
        if (context == WorkContext::Isr || context == WorkContext::Setup) return {WaitStatus::Forbidden, 0};
        if (!active) return {WaitStatus::Idle, 0};
        const uint32_t elapsed = now - start;
        if (attempt()) { active = false; return {WaitStatus::Completed, elapsed}; }
        if (elapsed >= timeout) { active = false; return {WaitStatus::TimedOut, elapsed}; }
        return {WaitStatus::Pending, elapsed};
    }
};
} // namespace experiment
