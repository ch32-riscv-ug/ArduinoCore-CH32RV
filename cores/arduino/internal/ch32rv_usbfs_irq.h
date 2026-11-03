/* The USBFS interrupt, shared by the device (CH32RVUsbDevice) and the host
 * (CH32RVUsbHost) libraries. The block runs one role at a time; the role that
 * starts sets the hook, and the handler calls whatever is set. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

extern void (*volatile ch32rv_usbfs_irq_hook)(void);

#ifdef __cplusplus
}
#endif
