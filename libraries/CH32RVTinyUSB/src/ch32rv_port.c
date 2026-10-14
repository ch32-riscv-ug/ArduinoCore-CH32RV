/* What this core adds to the vendored TinyUSB without patching it (listed as
 * OURS in tools/vendor/vendor_tinyusb.py). */
#include "tusb_option.h"

/* A failed TU_ASSERT returns false and the caller reports it; it must not stop
 * the CPU. TinyUSB's default on RISC-V is an unconditional ebreak, which halts
 * with a debugger attached and traps without one. */
void ch32rv_tusb_breakpoint(void)
{
}

#if CFG_TUD_ENABLED && CFG_TUSB_MCU == OPT_MCU_CH32X035
#include "device/dcd.h"
#include "portable/wch/ch32_usbfs_reg.h"

/* Upstream's USBFS driver has no dcd_deinit, so tusb_deinit() - and with it
 * CH32RVUsbDevice::end() - failed. Reset the controller to its state before
 * dcd_init(); usbd has already disabled the interrupt and the pull-up. */
bool dcd_deinit(uint8_t rhport)
{
    (void)rhport;
    USBOTG_FS->INT_EN = 0;
    USBOTG_FS->BASE_CTRL = USBFS_CTRL_RESET_SIE | USBFS_CTRL_CLR_ALL;
    USBOTG_FS->UDEV_CTRL = 0;
    USBOTG_FS->DEV_ADDR = 0;
    USBOTG_FS->INT_FG = 0xFF;
    USBOTG_FS->BASE_CTRL = 0;
    return true;
}
#endif
