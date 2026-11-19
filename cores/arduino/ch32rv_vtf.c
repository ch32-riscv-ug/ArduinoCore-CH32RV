/* VTF slots (ch32rv_priority.h): PFIC VTFIDR (0x50, a byte a slot: the
 * interrupt's number) and VTFADDR (0x60, a word a slot: the handler's
 * address, bit 0 the slot's enable) - device-data registers.csv's PFIC, the
 * same offsets on every family but V103 (whose VTFBADDRR / VTFADDRR are
 * another layout and get no slots). The address is the vector table's own
 * entry, so the handler is the one the interrupt had. */
#include <stdbool.h>
#include <stdint.h>

#include "ch32rv_priority.h"
#include "internal/ch32rv_registers.h"

#if CH32RV_IRQ_FAST_SLOTS > 0

#define VTFIDR(n)  CH32RV_REG8(CH32RV_PFIC_BASE + 0x50u + (n))
#define VTFADDR(n) CH32RV_REG32(CH32RV_PFIC_BASE + 0x60u + 4u * (n))
#define VTF_ENABLE 1u

extern const uint32_t _vector_base[];

static int slot_of(uint32_t irqn)
{
    for (int n = 0; n < CH32RV_IRQ_FAST_SLOTS; n++) {
        if ((VTFADDR(n) & VTF_ENABLE) && VTFIDR(n) == irqn) {
            return n;
        }
    }
    return -1;
}

bool ch32rv_irq_is_fast(uint32_t irqn)
{
    return slot_of(irqn) >= 0;
}

bool ch32rv_irq_set_fast(uint32_t irqn, bool on)
{
    int n = slot_of(irqn);
    if (!on) {
        if (n >= 0) {
            VTFADDR(n) = 0;
        }
        return true;
    }
    if (n >= 0) {
        return true;
    }
    for (n = 0; n < CH32RV_IRQ_FAST_SLOTS; n++) {
        if (!(VTFADDR(n) & VTF_ENABLE)) {
            const uint32_t handler = _vector_base[irqn];
            if (!handler || irqn > 255u) {
                return false;
            }
            VTFIDR(n) = (uint8_t)irqn;
            VTFADDR(n) = (handler & ~1u) | VTF_ENABLE;
            return true;
        }
    }
    return false;
}

#else

bool ch32rv_irq_is_fast(uint32_t irqn)
{
    (void)irqn;
    return false;
}

bool ch32rv_irq_set_fast(uint32_t irqn, bool on)
{
    (void)irqn;
    return !on;
}

#endif
