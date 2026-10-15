/* Ours (tools/vendor/vendor_tinyusb.py OURS).
 *
 * The vendored USBD (ST "fsdev" compatible) driver, compiled a second time
 * under ud_dcd_* names so it can sit next to the USBFS one behind the
 * dispatcher in ch32rv_dcd_dual.c, on V203/V208 (CH32RV_TUSB_DUAL_USBD).
 * Its helpers are static apart from fsdev_common.c's, which are built only
 * here (everywhere else tusb_config.h leaves the USBD path off). */
#define CH32RV_DCD_BUILDING_USBD 1     /* tusb_config.h: FSDEV 1 in this TU */
#include "tusb_option.h"

#if CFG_TUD_ENABLED && defined(CH32RV_TUSB_DUAL_USBD)

#define dcd_init                  ud_dcd_init
#define dcd_deinit                ud_dcd_deinit
#define dcd_int_handler           ud_dcd_int_handler
#define dcd_int_enable            ud_dcd_int_enable
#define dcd_int_disable           ud_dcd_int_disable
#define dcd_set_address           ud_dcd_set_address
#define dcd_remote_wakeup         ud_dcd_remote_wakeup
#define dcd_connect               ud_dcd_connect
#define dcd_disconnect            ud_dcd_disconnect
#define dcd_sof_enable            ud_dcd_sof_enable
#define dcd_edpt0_status_complete ud_dcd_edpt0_status_complete
#define dcd_edpt_open             ud_dcd_edpt_open
#define dcd_edpt_close_all        ud_dcd_edpt_close_all
#define dcd_edpt_xfer             ud_dcd_edpt_xfer
#define dcd_edpt_xfer_fifo        ud_dcd_edpt_xfer_fifo
#define dcd_edpt_stall            ud_dcd_edpt_stall
#define dcd_edpt_clear_stall      ud_dcd_edpt_clear_stall
#define dcd_edpt_iso_alloc        ud_dcd_edpt_iso_alloc
#define dcd_edpt_iso_activate     ud_dcd_edpt_iso_activate

#include "portable/st/stm32_fsdev/dcd_stm32_fsdev.c"
#include "portable/st/stm32_fsdev/fsdev_common.c"

/* The packet-memory copies fsdev uses. common/tusb_fifo.c has them only when
 * the whole build is in "dedicated hardware FIFO" mode, which the rest of
 * this build is not (the USBFS driver has no FIFO transfers, so the classes
 * must hand it buffers). On CH32V20x the 512-byte packet memory is 16-bit
 * data at a 32-bit address stride (CFG_TUSB_FSDEV_PMA_SIZE 512); fsdev always
 * passes a NULL access mode. */
void tu_hwfifo_write(volatile void *hwfifo, const uint8_t *src, uint16_t len, const tu_hwfifo_access_t *access_mode)
{
    (void)access_mode;
    volatile uint32_t *word = (volatile uint32_t *)hwfifo;
    while (len >= 2) {
        *(volatile uint16_t *)word++ = (uint16_t)(src[0] | (src[1] << 8));
        src += 2;
        len -= 2;
    }
    if (len) {
        *(volatile uint16_t *)word = src[0];
    }
}

void tu_hwfifo_read(const volatile void *hwfifo, uint8_t *dest, uint16_t len, const tu_hwfifo_access_t *access_mode)
{
    (void)access_mode;
    const volatile uint32_t *word = (const volatile uint32_t *)hwfifo;
    while (len >= 2) {
        const uint16_t v = *(const volatile uint16_t *)word++;
        dest[0] = (uint8_t)v;
        dest[1] = (uint8_t)(v >> 8);
        dest += 2;
        len -= 2;
    }
    if (len) {
        dest[0] = (uint8_t)*(const volatile uint16_t *)word;
    }
}

#endif
