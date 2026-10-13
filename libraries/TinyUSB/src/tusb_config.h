/* TinyUSB configuration for this core (ADR-0012). Device only, no RTOS.
 *
 * Only the series below build. TinyUSB's own register header includes the WCH
 * SDK header for the target (ch32x035.h); this directory supplies it. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
#define CFG_TUSB_MCU             OPT_MCU_CH32X035
#else
#error "TinyUSB: USB device is not wired up for this series yet"
#endif

#define CFG_TUSB_OS              OPT_OS_NONE
#define CFG_TUSB_DEBUG           0

#define CFG_TUD_ENABLED          1
#define CFG_TUH_ENABLED          0
#define CFG_TUD_MAX_SPEED        OPT_MODE_FULL_SPEED

/* The USBFS DMA reads endpoint buffers straight from SRAM. */
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN       __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE   64

#define CFG_TUD_CDC              1
#define CFG_TUD_MSC              0
#define CFG_TUD_HID              0
#define CFG_TUD_MIDI             0
#define CFG_TUD_VENDOR           0
#define CFG_TUD_DFU_RUNTIME      0

#define CFG_TUD_CDC_RX_BUFSIZE   256
#define CFG_TUD_CDC_TX_BUFSIZE   256
#define CFG_TUD_CDC_EP_BUFSIZE   64

#ifdef __cplusplus
}
#endif
