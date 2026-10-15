/* Ours (tools/vendor/vendor_tinyusb.py OURS).
 *
 * The vendored USBFS driver, compiled a second time under fs_dcd_* names so it
 * can sit next to the USBHS one (ch32rv_dcd_usbhs.c) behind the dispatcher in
 * ch32rv_dcd_dual.c. Only on the V30x parts with both controllers
 * (CH32RV_TUSB_DUAL_DCD, tusb_config.h); V303 uses the vendored file as is.
 *
 * Every other symbol in dcd_ch32_usbfs.c is static (data, update_in,
 * update_out, reset_ep_ctrls, ep_* helpers), so the dcd_* entry points are the
 * only names that need moving. The macros must be in place before
 * device/dcd.h is first seen, so its prototypes are renamed too. */
#define CH32RV_DCD_BUILDING_USBFS 1    /* tusb_config.h: USBFS 1 in this TU */
#include "tusb_option.h"

#if CFG_TUD_ENABLED && defined(CH32RV_TUSB_DUAL_DCD)

#define dcd_init                  fs_dcd_init
#define dcd_deinit                fs_dcd_deinit
#define dcd_int_handler           fs_dcd_int_handler
#define dcd_int_enable            fs_dcd_int_enable
#define dcd_int_disable           fs_dcd_int_disable
#define dcd_set_address           fs_dcd_set_address
#define dcd_remote_wakeup         fs_dcd_remote_wakeup
#define dcd_connect               fs_dcd_connect
#define dcd_disconnect            fs_dcd_disconnect
#define dcd_sof_enable            fs_dcd_sof_enable
#define dcd_edpt0_status_complete fs_dcd_edpt0_status_complete
#define dcd_edpt_open             fs_dcd_edpt_open_vendored  /* wrapped below */
#define dcd_edpt_close            fs_dcd_edpt_close
#define dcd_edpt_close_all        fs_dcd_edpt_close_all
#define dcd_edpt_xfer             fs_dcd_edpt_xfer
#define dcd_edpt_xfer_fifo        fs_dcd_edpt_xfer_fifo
#define dcd_edpt_stall            fs_dcd_edpt_stall
#define dcd_edpt_clear_stall      fs_dcd_edpt_clear_stall
#define dcd_edpt_iso_alloc        fs_dcd_edpt_iso_alloc
#define dcd_edpt_iso_activate     fs_dcd_edpt_iso_activate

#include "portable/wch/dcd_ch32_usbfs.c"

/* Upstream has no dcd_deinit (same as on X035, ch32rv_port.c). */
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

/* With USBHS linked, tusb_mcu.h turns on TUP_DCD_EDPT_CLOSE_API for the whole
 * build, so usbd_edpt_close() calls dcd_edpt_close(), which the FS driver
 * does not have (it has the iso_alloc/activate pair instead). NAK the
 * endpoint and forget the transfer; the next dcd_edpt_open() re-arms it. */
void dcd_edpt_close(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;
    const uint8_t ep = tu_edpt_number(ep_addr);
    const uint8_t dir = tu_edpt_dir(ep_addr);
    if (ep == 0 || ep >= EP_MAX) {
        return;
    }
    data.xfer[ep][dir].valid = false;
    data.isochronous[ep][dir] = false;
    if (dir == TUSB_DIR_OUT) {
        ep_rx_set_response(ep, USBFS_EP_R_RES_NAK);
    } else {
        EP_TX_LEN(ep) = 0;
        ep_tx_set_response(ep, USBFS_EP_T_RES_NAK);
    }
}

/* An iso endpoint goes through dcd_edpt_open() too in this build (usbd only
 * has the close API, TUP_DCD_EDPT_ISO_ALLOC is not set for V307), but the FS
 * driver learns "isochronous" only from dcd_edpt_iso_alloc(); its own open
 * clears the flag (dcd_ch32_usbfs.c:476). Route iso through alloc+activate. */
#undef dcd_edpt_open
bool fs_dcd_edpt_open(uint8_t rhport, const tusb_desc_endpoint_t *desc_ep)
{
    if (desc_ep->bmAttributes.xfer == TUSB_XFER_ISOCHRONOUS) {
        TU_ASSERT(fs_dcd_edpt_iso_alloc(rhport, desc_ep->bEndpointAddress, tu_edpt_packet_size(desc_ep)));
        return fs_dcd_edpt_iso_activate(rhport, desc_ep);
    }
    return fs_dcd_edpt_open_vendored(rhport, desc_ep);
}

#endif
