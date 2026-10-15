/* See ch32rv_usbfs_irq.h. Only on the series whose USBFS both libraries use;
 * elsewhere the device library keeps its own handler. Linked through the hook:
 * a sketch that uses neither library does not get this handler. */
#include "tusb_option.h"

#if CH32RV_TINYUSB_HOST

#include "ch32rv_usbfs_irq.h"

void (*volatile ch32rv_usbfs_irq_hook)(void);

__attribute__((interrupt)) void USBFS_IRQHandler(void)
{
    void (*hook)(void) = ch32rv_usbfs_irq_hook;
    if (hook) {
        hook();
    }
}

#endif
