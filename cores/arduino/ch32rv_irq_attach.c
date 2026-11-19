/* Interrupt handlers attached at run time (ch32rv_priority.h: ch32rv_irq_attach()). Linked only when something
 * attaches one; then its ch32rv_irq_unhandled replaces crt0's weak loop, which Default_Handler - every vector no
 * handler is linked for - jumps to. */
#include <stdbool.h>
#include <stdint.h>

#include "ch32rv_priority.h"
#include "internal/ch32rv_interrupt.h"
#include "internal/ch32rv_irq.h"
#include "internal/ch32rv_registers.h"

typedef struct {
    ch32rv_irq_handler_t fn;     /* null: a free slot */
    void *arg;
    uint32_t irqn;
} slot_t;

static slot_t slots[CH32RV_IRQ_ATTACH_SLOTS];

extern const uint32_t _vector_base[];
void Default_Handler(void);

CH32RV_INTERRUPT void ch32rv_irq_unhandled(void)
{
    uint32_t cause;
    __asm__ volatile ("csrr %0, mcause" : "=r"(cause));
    cause &= 0xFFFu;                 /* the interrupt's number */
    for (unsigned i = 0; i < CH32RV_IRQ_ATTACH_SLOTS; i++) {
        if (slots[i].fn && slots[i].irqn == cause) {
            slots[i].fn(slots[i].arg);
            return;
        }
    }
    for (;;) {                       /* as before: an interrupt nothing handles */
    }
}

static int slot_of(uint32_t irqn)
{
    for (unsigned i = 0; i < CH32RV_IRQ_ATTACH_SLOTS; i++) {
        if (slots[i].fn && slots[i].irqn == irqn) {
            return (int)i;
        }
    }
    return -1;
}

bool ch32rv_irq_attach(uint32_t irqn, ch32rv_irq_handler_t fn, void *arg)
{
    if (!fn || irqn < 16u || irqn > 255u) {
        return false;
    }
#if !defined(CH32RV_SERIES_CH32V103)
    /* an address table: the vector's entry is Default_Handler unless something links a handler for it */
    if (_vector_base[irqn] != (uint32_t)(uintptr_t)Default_Handler) {
        return false;
    }
#endif
    const uint32_t irq = ch32rv_irq_save();
    int n = slot_of(irqn);
    for (unsigned i = 0; n < 0 && i < CH32RV_IRQ_ATTACH_SLOTS; i++) {
        if (!slots[i].fn) {
            n = (int)i;
        }
    }
    if (n >= 0) {
        slots[n].irqn = irqn;
        slots[n].arg = arg;
        slots[n].fn = fn;
    }
    ch32rv_irq_restore(irq);
    if (n < 0) {
        return false;
    }
    ch32rv_irq_enable(irqn);
    return true;
}

void ch32rv_irq_detach(uint32_t irqn)
{
    ch32rv_irq_disable(irqn);
    const uint32_t irq = ch32rv_irq_save();
    const int n = slot_of(irqn);
    if (n >= 0) {
        slots[n].fn = 0;
    }
    ch32rv_irq_restore(irq);
}
