/* The USBPD interrupt, shared by the sink (CH32RVUsbPd) and the source
 * (CH32RVUsbPdSource): the block is one, the role that began owns it. Its own
 * file so that a sketch using either role links it once. */
#include "usbpd_hw.h"

#ifdef CH32RV_USBPD_BASE

void (*ch32rv_usbpd_irq_hook)(void);

extern "C" __attribute__((interrupt)) void CH32RV_USBPD_IRQ_HANDLER(void)
{
    if (ch32rv_usbpd_irq_hook) {
        ch32rv_usbpd_irq_hook();
    }
}

#endif
