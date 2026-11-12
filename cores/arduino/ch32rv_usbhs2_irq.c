/* See internal/ch32rv_usbhs2.h. Linked only through the hooks: a sketch that
 * uses neither USB library keeps the default (weak) vectors. */
#include "internal/ch32rv_usbhs2.h"
#include "internal/ch32rv_interrupt.h"

#ifdef CH32RV_USBHS2_UNITS

void (*volatile ch32rv_usbhs2_irq_hook[2])(void);

#if CH32RV_USBHS2_UNITS == 2
CH32RV_INTERRUPT void USBHS1_IRQHandler(void)
{
    void (*hook)(void) = ch32rv_usbhs2_irq_hook[0];
    if (hook) {
        hook();
    }
}

CH32RV_INTERRUPT void USBHS2_IRQHandler(void)
{
    void (*hook)(void) = ch32rv_usbhs2_irq_hook[1];
    if (hook) {
        hook();
    }
}
#else
CH32RV_INTERRUPT void USBHS_IRQHandler(void)
{
    void (*hook)(void) = ch32rv_usbhs2_irq_hook[0];
    if (hook) {
        hook();
    }
}
#endif

#endif
