/* See ch32rv_usbfs_irq.h. Linked only through the hook: a sketch that uses
 * neither USB library keeps the default (weak) vector. */
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_usbfs_irq.h"
#include "internal/ch32rv_interrupt.h"

#ifdef CH32RV_IRQN_USBFS

void (*volatile ch32rv_usbfs_irq_hook)(void);

CH32RV_INTERRUPT void USBFS_IRQHandler(void)
{
    void (*hook)(void) = ch32rv_usbfs_irq_hook;
    if (hook) {
        hook();
    }
}

#endif
