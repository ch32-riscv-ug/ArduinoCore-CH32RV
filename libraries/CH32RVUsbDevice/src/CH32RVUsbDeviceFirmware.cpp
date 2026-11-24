/* The end of a firmware update through USB (CH32RVUsbDeviceDfu,
 * CH32RVUsbDeviceMscFirmwareDisk): leave the bus once the host has its last
 * answer, then install the staged image (ch32rv_update_restart()). From the
 * device task, outside any USB callback: end() must not run inside one. */
#include "internal/CH32RVUsbDeviceInternal.h"

#include <internal/ch32rv_update.h>

static CH32RVUsbDevice *leaving;
static uint32_t leave_at;

static void leave_when_due()
{
    if ((int32_t)(millis() - leave_at) < 0) {
        ch32rv_usb_deferred = leave_when_due;          /* not yet: the next round of the task */
        return;
    }
    if (leaving) {
        leaving->end();
    }
    const uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 50u) {           /* long enough for the host to see it go */
    }
    ch32rv_update_restart();
}

void ch32rv_usb_install_after(CH32RVUsbDevice &device, uint32_t ms)
{
    leaving = &device;
    leave_at = millis() + ms;
    ch32rv_usb_deferred = leave_when_due;
}
