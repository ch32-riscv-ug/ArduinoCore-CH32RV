/* What this core adds to the vendored TinyUSB without patching it (listed as
 * OURS in tools/vendor/vendor_tinyusb.py). */
#include <stdint.h>

#include "tusb_option.h"

/* A failed TU_ASSERT returns false and the caller reports it; it must not stop
 * the CPU. TinyUSB's default on RISC-V is an unconditional ebreak, which halts
 * with a debugger attached and traps without one. */
void ch32rv_tusb_breakpoint(void)
{
}

/* The single-controller USBFS builds (X035/X033, V103, V303); the dual builds
 * (V20x USBFS + USBD, V305/V307/V317 USBFS + USBHS) get theirs in
 * ch32rv_dcd_usbfs.c. */
#if CFG_TUD_ENABLED && defined(TUP_USBIP_WCH_USBFS) && CFG_TUD_WCH_USBIP_USBFS && !defined(CH32RV_TUSB_DUAL_DCD)
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

/* TinyUSB's clock: the device stack's and the host's (deferred calls,
 * debouncing, timeouts). */
#if CFG_TUD_ENABLED || CFG_TUH_ENABLED
extern unsigned long millis(void);
extern void delay(unsigned long ms);

uint32_t tusb_time_millis_api(void)
{
    return (uint32_t)millis();
}

void tusb_time_delay_ms_api(uint32_t ms)
{
    delay(ms);
}
#endif
