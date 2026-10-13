/* TinyUSB configuration for this core (ADR-0012). Device only, no RTOS.
 *
 * Only the series below build. TinyUSB's own register header includes the WCH
 * SDK header for the target (ch32x035.h); this directory supplies it. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* CH32RV_TINYUSB_DEVICE says whether this series has the device wired up. On
 * the others the stack compiles to nothing and SerialUSB is a port that never
 * opens (begin() returns, operator bool stays false), so a sketch still builds
 * for every board - the same contract USBPD keeps. */
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
#define CH32RV_TINYUSB_DEVICE    1
#define CFG_TUSB_MCU             OPT_MCU_CH32X035
#else
#define CH32RV_TINYUSB_DEVICE    0
#define CFG_TUSB_MCU             OPT_MCU_NONE
#endif

#define CFG_TUSB_OS              OPT_OS_NONE
#define CFG_TUSB_DEBUG           0

#define CFG_TUD_ENABLED          CH32RV_TINYUSB_DEVICE
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
