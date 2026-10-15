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

#elif defined(CH32RV_SERIES_CH32V103)
/* USBFS, the older IP (one control register per endpoint). */
#define CH32RV_TINYUSB_DEVICE    1
#define CFG_TUSB_MCU             OPT_MCU_CH32V103

#elif defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208)
/* USBFS (OTG_FS, PB6/PB7). The USBD block (PA11/PA12, the parts that have
 * only that one) is not wired up yet. */
#define CH32RV_TINYUSB_DEVICE    1
#define CFG_TUSB_MCU             OPT_MCU_CH32V20X
#define CFG_TUD_WCH_USBIP_USBFS  1

#elif defined(CH32RV_SERIES_CH32V303)
/* USBFS only: V303 has no USBHS. */
#define CH32RV_TINYUSB_DEVICE    1
#define CFG_TUSB_MCU             OPT_MCU_CH32V307
#define CFG_TUD_WCH_USBIP_USBFS  1
#define CFG_TUD_WCH_USBIP_USBHS  0

#elif defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || \
      defined(CH32RV_SERIES_CH32V317)
/* USBFS and USBHS (its own PHY). Both drivers are built, under fs_ / hs_
 * names (ch32rv_dcd_usbfs.c, ch32rv_dcd_usbhs.c), and ch32rv_dcd_dual.c
 * forwards TinyUSB's dcd_* to the one CH32RVUsbDeviceConfig::controller chose.
 *
 * The vendored dcd_ch32_usbfs.c / dcd_ch32_usbhs.c are still compiled on
 * their own (the builder compiles every .c under src/) and must come out
 * empty: USBFS 0 empties the FS one; USBHS 2 empties the HS one (it tests
 * == 1) while tusb_mcu.h, which only tests it for truth, still builds usbd
 * for a high-speed port with 16 endpoints and the close API. */
#define CH32RV_TINYUSB_DEVICE    1
#define CH32RV_TUSB_DUAL_DCD     1
#define CFG_TUSB_MCU             OPT_MCU_CH32V307
#if defined(CH32RV_DCD_BUILDING_USBFS)
#define CFG_TUD_WCH_USBIP_USBFS  1
#define CFG_TUD_WCH_USBIP_USBHS  2
#elif defined(CH32RV_DCD_BUILDING_USBHS)
#define CFG_TUD_WCH_USBIP_USBFS  0
#define CFG_TUD_WCH_USBIP_USBHS  1
#else
#define CFG_TUD_WCH_USBIP_USBFS  0
#define CFG_TUD_WCH_USBIP_USBHS  2
#endif
#define CFG_TUD_MAX_SPEED        OPT_MODE_HIGH_SPEED

#else
#define CH32RV_TINYUSB_DEVICE    0
#define CFG_TUSB_MCU             OPT_MCU_NONE
#endif

#define CFG_TUSB_OS              OPT_OS_NONE
#define CFG_TUSB_DEBUG           0
/* A failed assertion returns false instead of executing ebreak (ch32rv_port.c). */
#define CFG_TUSB_DEBUG_BREAKPOINT ch32rv_tusb_breakpoint

#define CFG_TUD_ENABLED          CH32RV_TINYUSB_DEVICE
#define CFG_TUH_ENABLED          0
#ifndef CFG_TUD_MAX_SPEED
#define CFG_TUD_MAX_SPEED        OPT_MODE_FULL_SPEED
#endif

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
#ifndef CFG_TUD_MSC
#define CFG_TUD_MSC              1
#endif
#ifndef CFG_TUD_VENDOR
#define CFG_TUD_VENDOR           1
#endif
#ifndef CFG_TUD_NCM
#define CFG_TUD_NCM              1
#endif

/* Buffers, per instance. CDC 256 B each way keeps a 115200-baud burst; 64 B
 * saves 384 B of RAM per port on a part that needs it. */
#ifndef CFG_TUD_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_RX_BUFSIZE   256
#endif
#ifndef CFG_TUD_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE   256
#endif
/* One packet of the fastest speed built in: 64 at full speed, 512 at high. */
#if CFG_TUD_MAX_SPEED == OPT_MODE_HIGH_SPEED
#define CFG_TUD_CDC_EP_BUFSIZE   512
#else
#define CFG_TUD_CDC_EP_BUFSIZE   64
#endif
#ifndef CFG_TUD_HID_EP_BUFSIZE
#define CFG_TUD_HID_EP_BUFSIZE   64
#endif
#ifndef CFG_TUD_MIDI_RX_BUFSIZE
#define CFG_TUD_MIDI_RX_BUFSIZE  64
#endif
#ifndef CFG_TUD_MIDI_TX_BUFSIZE
#define CFG_TUD_MIDI_TX_BUFSIZE  64
#endif
/* Vendor (bulk): the FIFOs behind available()/read() and write(). */
#ifndef CFG_TUD_VENDOR_RX_BUFSIZE
#define CFG_TUD_VENDOR_RX_BUFSIZE 128
#endif
#ifndef CFG_TUD_VENDOR_TX_BUFSIZE
#define CFG_TUD_VENDOR_TX_BUFSIZE 128
#endif
/* NCM: one transfer block each way, the size the NCM spec sets as the
 * minimum a host must accept (2048); 4.2 KB of RAM once declared. */
#ifndef CFG_TUD_NCM_IN_NTB_MAX_SIZE
#define CFG_TUD_NCM_IN_NTB_MAX_SIZE  2048
#endif
#ifndef CFG_TUD_NCM_OUT_NTB_MAX_SIZE
#define CFG_TUD_NCM_OUT_NTB_MAX_SIZE 2048
#endif
/* MSC: one transfer buffer; a block (512) moves in one piece. */
#ifndef CFG_TUD_MSC_EP_BUFSIZE
#define CFG_TUD_MSC_EP_BUFSIZE   512
#endif

#ifdef __cplusplus
}
#endif
