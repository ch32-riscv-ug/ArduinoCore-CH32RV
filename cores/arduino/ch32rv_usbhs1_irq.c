/* See internal/ch32rv_usbhs1.h. Linked only through the hook: a sketch that
 * uses neither USB library keeps the default (weak) vector. */
#include "internal/ch32rv_usbhs1.h"
#include "internal/ch32rv_interrupt.h"

#ifdef CH32RV_USBHS1_BASE

void (*volatile ch32rv_usbhs1_irq_hook)(void);

CH32RV_INTERRUPT void USBHS_IRQHandler(void)
{
    void (*hook)(void) = ch32rv_usbhs1_irq_hook;
    if (hook) {
        hook();
    }
}

#endif
