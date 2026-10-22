/* CH32RVSyncKit: what every part shares - how a part is protected against
 * this core's interrupts, and the time source the deadline parts read.
 *
 * Sharing is chosen per object, as a template argument, and is visible in the
 * code that declares it:
 *
 *   IsrSafe   (the default) every operation runs with this core's interrupts
 *             masked for a few instructions, so an ISR and loop() may both
 *             use the object. On CH32V003 (no atomic instructions) this is the
 *             only way; on the larger parts it is still the cheapest correct
 *             one for multi-field state.
 *   LoopOnly  no masking at all: the object is used from normal code only
 *             (loop(), setup(), functions they call). Smaller and faster;
 *             touching it from an ISR is a bug.
 *
 * Neither is safe across cores (CH32H41x's second core keeps running while
 * this one masks its interrupts): this library is for one core and its ISRs.
 *
 * No part blocks. Every operation tries once and returns; the caller keeps
 * its state and tries again later (the next loop(), a later step). There is
 * no receive(timeout) - waiting in a loop of its own stops everything else
 * this core does. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ch32rv_irq.h"

extern "C" unsigned long millis(void);

namespace CH32RVSyncKit {

/* Masks this core's interrupts for the life of the object and puts them back
 * as they were (so it nests, and is safe to use where interrupts are already
 * off). Keep the section to a few lines: every interrupt waits for it, and
 * USB, USB PD and the UARTs have deadlines of their own.
 *
 *     {
 *         CH32RVSyncKit::CriticalSection cs;    // interrupts off
 *         a = shared_a; b = shared_b;           // one consistent snapshot
 *     }                                         // interrupts back on
 */
class CriticalSection {
public:
    CriticalSection() : _saved(ch32rv_irq_save()) {}
    ~CriticalSection() { ch32rv_irq_restore(_saved); }
    CriticalSection(const CriticalSection &) = delete;
    CriticalSection &operator=(const CriticalSection &) = delete;

private:
    uint32_t _saved;
};

/* Sharing policies (see the top of this file). Each provides a Guard that is
 * alive for one operation. */
struct IsrSafe {
    using Guard = CriticalSection;
};

struct LoopOnly {
    struct Guard {
        Guard() {}
        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;
    };
};

/* The time source the deadline parts read when no time is passed in. */
inline uint32_t now()
{
    return (uint32_t)millis();
}

}  // namespace CH32RVSyncKit
