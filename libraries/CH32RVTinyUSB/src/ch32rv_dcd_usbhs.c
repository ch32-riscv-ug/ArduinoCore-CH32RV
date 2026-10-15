/* Ours (tools/vendor/vendor_tinyusb.py OURS).
 *
 * The vendored USBHS driver under hs_dcd_* names, for ch32rv_dcd_dual.c.
 * Its other symbols are all static (xfer_status, ep0_buffer, ep0_tog,
 * queue_in_packet, queue_out_packet, update_in, update_out). */
#define CH32RV_DCD_BUILDING_USBHS 1    /* tusb_config.h: USBHS 1 in this TU */
#include "tusb_option.h"

#if CFG_TUD_ENABLED && defined(CH32RV_TUSB_DUAL_DCD)

#define dcd_init                  hs_dcd_init
#define dcd_deinit                hs_dcd_deinit
#define dcd_int_handler           hs_dcd_int_handler_vendored   /* wrapped below */
#define dcd_int_enable            hs_dcd_int_enable
#define dcd_int_disable           hs_dcd_int_disable
#define dcd_set_address           hs_dcd_set_address
#define dcd_remote_wakeup         hs_dcd_remote_wakeup
#define dcd_connect               hs_dcd_connect
#define dcd_disconnect            hs_dcd_disconnect
#define dcd_sof_enable            hs_dcd_sof_enable
#define dcd_edpt0_status_complete hs_dcd_edpt0_status_complete
#define dcd_edpt_open             hs_dcd_edpt_open
#define dcd_edpt_close            hs_dcd_edpt_close
#define dcd_edpt_close_all        hs_dcd_edpt_close_all
#define dcd_edpt_xfer             hs_dcd_edpt_xfer
#define dcd_edpt_xfer_fifo        hs_dcd_edpt_xfer_fifo
#define dcd_edpt_stall            hs_dcd_edpt_stall
#define dcd_edpt_clear_stall      hs_dcd_edpt_clear_stall
#define dcd_edpt_iso_alloc        hs_dcd_edpt_iso_alloc
#define dcd_edpt_iso_activate     hs_dcd_edpt_iso_activate

#include "portable/wch/dcd_ch32_usbhs.c"

/* ---- what upstream's USBHS driver lacks ---- */

/* dcd_init() connects by itself (CONTROL |= DEV_PU_EN, dcd_ch32_usbhs.c:181);
 * these let tud_disconnect()/tud_connect() and tud_deinit() work. */
void dcd_connect(uint8_t rhport)
{
    (void)rhport;
    USBHSD->CONTROL |= USBHS_DEV_PU_EN;
}

void dcd_disconnect(uint8_t rhport)
{
    (void)rhport;
    USBHSD->CONTROL &= (uint8_t)~USBHS_DEV_PU_EN;
}

bool dcd_deinit(uint8_t rhport)
{
    (void)rhport;
    USBHSD->INT_EN = 0;
    USBHSD->CONTROL = USBHS_ALL_CLR | USBHS_FORCE_RST;  /* EVT USBHS_Device_Init(DISABLE) */
    USBHSD->CONTROL = 0;
    USBHSD->ENDP_CONFIG = 0;
    USBHSD->DEV_AD = 0;
    USBHSD->INT_FG = 0xFF;
    USBHSD->HOST_CTRL = 0;                              /* PHY back to suspend */
    return true;
}

/* The vendored handler always reports a bus reset as TUSB_SPEED_HIGH
 * (dcd_ch32_usbhs.c:406-425), but the PHY falls back to full speed behind a
 * FS hub or host, and R8_USB_SPEED_TYPE says which (RM V2.5 22.2.1.6). The
 * reset interrupt arrives before the chirp settles, so check at the first
 * SETUP after a reset and, when it is not HS, queue a second bus-reset
 * event with the real speed ahead of that SETUP: usbd resets again (nothing
 * is configured yet) and tud_speed_get() is right from then on.
 * UNTESTED - needs a FS-only hub on the bench. */
static volatile bool reset_speed_unchecked;

void hs_dcd_int_handler(uint8_t rhport)
{
    const uint8_t fg = USBHSD->INT_FG;
    /* Mirror the vendored else-if order: TRANSFER, then SETUP, then BUS_RST. */
    if (!(fg & USBHS_TRANSFER_FLAG) && (fg & USBHS_SETUP_FLAG) && reset_speed_unchecked) {
        reset_speed_unchecked = false;
        const uint8_t st = USBHSD->SPEED_TYPE & USBHS_SPEED_TYPE_MASK;
        if (st != USBHS_SPEED_TYPE_HIGH) {
            dcd_event_bus_reset(0, st == USBHS_SPEED_TYPE_LOW ? TUSB_SPEED_LOW : TUSB_SPEED_FULL, true);
        }
    } else if (!(fg & (USBHS_TRANSFER_FLAG | USBHS_SETUP_FLAG)) && (fg & USBHS_BUS_RST_FLAG)) {
        reset_speed_unchecked = true;
    }
    hs_dcd_int_handler_vendored(rhport);
}

#endif
