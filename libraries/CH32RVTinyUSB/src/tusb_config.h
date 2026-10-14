/* TinyUSB configuration for this core (ADR-0012, docs/usb.ja.md). Device
 * only for now, no RTOS.
 *
 * Every class CH32RVUsbDevice wraps is compiled, and none is in TinyUSB's
 * built-in driver table (CFG_TUD_APP_DRIVERS_ONLY, a patch recorded in
 * tools/vendor/vendor_tinyusb.py): a class object registers its driver when the
 * sketch declares it, so a class the sketch does not use is not linked.
 *
 * What stays fixed at build time is sizes: how many of each class, and their
 * buffers. The defaults suit a small part; a sketch that needs more says so in
 * its build_opt.h, e.g.
 *     -DCFG_TUD_CDC=2
 *     -DCFG_TUD_CDC_RX_BUFSIZE=1024
 *
 * TinyUSB's own register header includes the WCH SDK header for the target
 * (ch32x035.h); this directory supplies it. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* CH32RV_TINYUSB_DEVICE says whether this series has the device wired up. On
 * the others the stack compiles to nothing and CH32RVUsbDevice::begin()
 * returns false, so a sketch still builds for every board - the same contract
 * CH32RVUsbPd keeps. */
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

/* Drivers come from the class objects only (see the top of this file). */
#define CFG_TUD_APP_DRIVERS_ONLY 1

/* How many of each class a device can have. A class at 0 cannot be declared;
 * 1 costs nothing until a sketch declares one. */
#ifndef CFG_TUD_CDC
#define CFG_TUD_CDC              1
#endif
#ifndef CFG_TUD_HID
#define CFG_TUD_HID              1
#endif
#ifndef CFG_TUD_MIDI
#define CFG_TUD_MIDI             1
#endif
#ifndef CFG_TUD_DFU_RUNTIME
#define CFG_TUD_DFU_RUNTIME      1
#endif
#define CFG_TUD_MSC              0
#define CFG_TUD_VENDOR           0

/* Buffers, per instance. CDC 256 B each way keeps a 115200-baud burst; 64 B
 * saves 384 B of RAM per port on a part that needs it. */
#ifndef CFG_TUD_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_RX_BUFSIZE   256
#endif
#ifndef CFG_TUD_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE   256
#endif
#define CFG_TUD_CDC_EP_BUFSIZE   64
#ifndef CFG_TUD_HID_EP_BUFSIZE
#define CFG_TUD_HID_EP_BUFSIZE   64
#endif
#ifndef CFG_TUD_MIDI_RX_BUFSIZE
#define CFG_TUD_MIDI_RX_BUFSIZE  64
#endif
#ifndef CFG_TUD_MIDI_TX_BUFSIZE
#define CFG_TUD_MIDI_TX_BUFSIZE  64
#endif

#ifdef __cplusplus
}
#endif
