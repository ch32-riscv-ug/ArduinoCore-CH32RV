/* See internal/ch32rv_usbss.h. Linked only through the hooks: a sketch that
 * uses neither USB library keeps the default (weak) vectors. */
#include "internal/ch32rv_usbss.h"
#include "internal/ch32rv_interrupt.h"

#ifdef CH32RV_USBSS_BASE

void (*volatile ch32rv_usbss_irq_hook[2])(void);

CH32RV_INTERRUPT void USBSS_IRQHandler(void)
{
    void (*hook)(void) = ch32rv_usbss_irq_hook[CH32RV_USBSS_HOOK_USB];
    if (hook) {
        hook();
    }
}

CH32RV_INTERRUPT void USBSS_LINK_IRQHandler(void)
{
    void (*hook)(void) = ch32rv_usbss_irq_hook[CH32RV_USBSS_HOOK_LINK];
    if (hook) {
        hook();
    }
}

#endif
