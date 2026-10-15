/* Ours (tools/vendor/vendor_tinyusb.py OURS).
 *
 * The dcd_* that usbd.c calls, forwarded to the controller chosen at begin().
 *
 * Dispatch is on a variable, not on the rhport argument. The vendored USBHS
 * driver hard-codes rhport 0 in the events it posts (dcd_ch32_usbhs.c:402
 * dcd_event_setup_received(0, ...), :425 dcd_event_bus_reset(0, ...)), and
 * usbd.c hands an event's rhport straight back to the DCD (usbd.c:742-746
 * process_setup_received / dcd_edpt_stall(event.rhport, ...)), so with USBHS
 * on rhport 1 a STALL or SET_ADDRESS would be routed to the FS driver. The
 * device therefore always runs as rhport 0 (tusb_init(0, ...),
 * tud_int_handler(0) from both IRQ handlers) and this variable says which
 * hardware rhport 0 is. */
#include "tusb_option.h"

#if CFG_TUD_ENABLED && defined(CH32RV_TUSB_DUAL_DCD)
#include "device/dcd.h"
#include "ch32rv_dcd_dual.h"

#define CH32RV_DCD_DECLARE(ret, name, params) \
    ret fs_##name params;                     \
    ret hs_##name params;

CH32RV_DCD_DECLARE(bool, dcd_init, (uint8_t rhport, const tusb_rhport_init_t *rh_init))
CH32RV_DCD_DECLARE(bool, dcd_deinit, (uint8_t rhport))
CH32RV_DCD_DECLARE(void, dcd_int_handler, (uint8_t rhport))
CH32RV_DCD_DECLARE(void, dcd_int_enable, (uint8_t rhport))
CH32RV_DCD_DECLARE(void, dcd_int_disable, (uint8_t rhport))
CH32RV_DCD_DECLARE(void, dcd_set_address, (uint8_t rhport, uint8_t dev_addr))
CH32RV_DCD_DECLARE(void, dcd_remote_wakeup, (uint8_t rhport))
CH32RV_DCD_DECLARE(void, dcd_connect, (uint8_t rhport))
CH32RV_DCD_DECLARE(void, dcd_disconnect, (uint8_t rhport))
CH32RV_DCD_DECLARE(void, dcd_sof_enable, (uint8_t rhport, bool en))
CH32RV_DCD_DECLARE(void, dcd_edpt0_status_complete, (uint8_t rhport, const tusb_control_request_t *request))
CH32RV_DCD_DECLARE(bool, dcd_edpt_open, (uint8_t rhport, const tusb_desc_endpoint_t *desc_ep))
CH32RV_DCD_DECLARE(void, dcd_edpt_close, (uint8_t rhport, uint8_t ep_addr))
CH32RV_DCD_DECLARE(void, dcd_edpt_close_all, (uint8_t rhport))
CH32RV_DCD_DECLARE(bool, dcd_edpt_xfer, (uint8_t rhport, uint8_t ep_addr, uint8_t *buffer, uint16_t total_bytes, bool is_isr))
CH32RV_DCD_DECLARE(void, dcd_edpt_stall, (uint8_t rhport, uint8_t ep_addr))
CH32RV_DCD_DECLARE(void, dcd_edpt_clear_stall, (uint8_t rhport, uint8_t ep_addr))

/* Neither driver has dcd_edpt_xfer_fifo (usbd.c:445 keeps its weak "false"),
 * and dcd_edpt_iso_alloc/activate are not called by usbd for V307 (no
 * TUP_DCD_EDPT_ISO_ALLOC); ch32rv_dcd_usbfs.c routes FS iso through open. */

static uint8_t controller = CH32RV_DCD_USBFS;

bool ch32rv_dcd_select(uint8_t which)
{
    if (which != CH32RV_DCD_USBFS && which != CH32RV_DCD_USBHS) {
        return false;
    }
    controller = which;
    return true;
}

uint8_t ch32rv_dcd_selected(void)
{
    return controller;
}

#define HS (controller == CH32RV_DCD_USBHS)

bool dcd_init(uint8_t rhport, const tusb_rhport_init_t *rh_init)
{
    return HS ? hs_dcd_init(rhport, rh_init) : fs_dcd_init(rhport, rh_init);
}

bool dcd_deinit(uint8_t rhport)
{
    return HS ? hs_dcd_deinit(rhport) : fs_dcd_deinit(rhport);
}

void dcd_int_handler(uint8_t rhport)
{
    HS ? hs_dcd_int_handler(rhport) : fs_dcd_int_handler(rhport);
}

void dcd_int_enable(uint8_t rhport)
{
    HS ? hs_dcd_int_enable(rhport) : fs_dcd_int_enable(rhport);
}

void dcd_int_disable(uint8_t rhport)
{
    HS ? hs_dcd_int_disable(rhport) : fs_dcd_int_disable(rhport);
}

void dcd_set_address(uint8_t rhport, uint8_t dev_addr)
{
    HS ? hs_dcd_set_address(rhport, dev_addr) : fs_dcd_set_address(rhport, dev_addr);
}

void dcd_remote_wakeup(uint8_t rhport)
{
    HS ? hs_dcd_remote_wakeup(rhport) : fs_dcd_remote_wakeup(rhport);
}

void dcd_connect(uint8_t rhport)
{
    HS ? hs_dcd_connect(rhport) : fs_dcd_connect(rhport);
}

void dcd_disconnect(uint8_t rhport)
{
    HS ? hs_dcd_disconnect(rhport) : fs_dcd_disconnect(rhport);
}

void dcd_sof_enable(uint8_t rhport, bool en)
{
    HS ? hs_dcd_sof_enable(rhport, en) : fs_dcd_sof_enable(rhport, en);
}

void dcd_edpt0_status_complete(uint8_t rhport, const tusb_control_request_t *request)
{
    HS ? hs_dcd_edpt0_status_complete(rhport, request) : fs_dcd_edpt0_status_complete(rhport, request);
}

bool dcd_edpt_open(uint8_t rhport, const tusb_desc_endpoint_t *desc_ep)
{
    return HS ? hs_dcd_edpt_open(rhport, desc_ep) : fs_dcd_edpt_open(rhport, desc_ep);
}

void dcd_edpt_close(uint8_t rhport, uint8_t ep_addr)
{
    HS ? hs_dcd_edpt_close(rhport, ep_addr) : fs_dcd_edpt_close(rhport, ep_addr);
}

void dcd_edpt_close_all(uint8_t rhport)
{
    HS ? hs_dcd_edpt_close_all(rhport) : fs_dcd_edpt_close_all(rhport);
}

bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t *buffer, uint16_t total_bytes, bool is_isr)
{
    return HS ? hs_dcd_edpt_xfer(rhport, ep_addr, buffer, total_bytes, is_isr)
              : fs_dcd_edpt_xfer(rhport, ep_addr, buffer, total_bytes, is_isr);
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr)
{
    HS ? hs_dcd_edpt_stall(rhport, ep_addr) : fs_dcd_edpt_stall(rhport, ep_addr);
}

void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr)
{
    HS ? hs_dcd_edpt_clear_stall(rhport, ep_addr) : fs_dcd_edpt_clear_stall(rhport, ep_addr);
}

#endif
