/* The cores' lock (internal/ch32rv_cores.h): CH32H41x with the sketch on the V5F, where the V3F may run the
 * sketch's code too. */
#include "internal/ch32rv_registers.h"

#if defined(CH32RV_IRQN_HSEM) && defined(CH32RV_RUN_V5F)

uint32_t ch32rv_cores_lock(void)
{
    const uint32_t irq = ch32rv_irq_save();
    /* The HSEM's register, not the shared SRAM: polling it back to back starves no bank (wiring_time.c). */
    while (!ch32rv_hsem_read_lock(CH32RV_HSEM_CORE_CHANNEL)) {
    }
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
    return irq;
}

void ch32rv_cores_unlock(uint32_t irq)
{
    ch32rv_hsem_unlock(CH32RV_HSEM_CORE_CHANNEL);
    ch32rv_irq_restore(irq);
}

#endif
