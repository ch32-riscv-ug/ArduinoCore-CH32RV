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
/* USBFS (OTG_FS, PB6/PB7) and USBD (ST fsdev compatible, PA11/PA12; some
 * parts have only one of them). Both are built, USBFS as fs_ and USBD as
 * ud_ (ch32rv_dcd_usbfs.c, ch32rv_dcd_usbd.c), and ch32rv_dcd_dual.c forwards
 * to the one CH32RVUsbDeviceConfig::controller chose. Everywhere else both
 * vendored drivers come out empty (USBFS 0, FSDEV 0). */
#define CH32RV_TINYUSB_DEVICE    1
#define CH32RV_TUSB_DUAL_DCD     1
#define CH32RV_TUSB_DUAL_USBD    1
#define CFG_TUSB_MCU             OPT_MCU_CH32V20X
#if defined(CH32RV_DCD_BUILDING_USBFS)
#define CFG_TUD_WCH_USBIP_USBFS  1
#define CFG_TUD_WCH_USBIP_FSDEV  0
#elif defined(CH32RV_DCD_BUILDING_USBD)
#define CFG_TUD_WCH_USBIP_USBFS  0
#define CFG_TUD_WCH_USBIP_FSDEV  1
#else
#define CFG_TUD_WCH_USBIP_USBFS  0
#define CFG_TUD_WCH_USBIP_FSDEV  0
#endif

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

/* CH32RV_TINYUSB_HOST: the series has the host side (CH32RVUsbHost) wired up.
 * Our own controller driver, ch32rv_hcd_usbfs.c, drives the USBFS block in
 * host mode; device and host share the block, one role at a time, so the
 * host runs on TinyUSB's rhport 1 while the device keeps rhport 0. */
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
#define CH32RV_TINYUSB_HOST      1
#define CH32RV_HCD_USBFS         1
#define CH32RV_HCD_USBFS_BASE    0x40023400u
#define CH32RV_HCD_USBFS_IRQ     CH32RV_IRQN_USBFS
#define CH32RV_HCD_USBFS_LAYOUT  1
#elif defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || \
      defined(CH32RV_SERIES_CH32V303) || defined(CH32RV_SERIES_CH32V305) || \
      defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317)
/* USBFS (OTG_FS): PB6 / PB7 on V20x, PA11 / PA12 on V30x. */
#define CH32RV_TINYUSB_HOST      1
#define CH32RV_HCD_USBFS         1
#define CH32RV_HCD_USBFS_BASE    0x50000000u
#define CH32RV_HCD_USBFS_IRQ     CH32RV_IRQN_USBFS
#define CH32RV_HCD_USBFS_LAYOUT  2
#else
#define CH32RV_TINYUSB_HOST      0
#endif
#define CH32RV_TUH_RHPORT        1
/* Upstream's portable/wch/hcd_ch32_usbfs.c stays out: ch32rv_hcd_usbfs.c is
 * the host driver here (tusb_mcu.h turns upstream's on for V20x by default). */
#define CFG_TUH_WCH_USBIP_USBFS  0

#define CFG_TUSB_OS              OPT_OS_NONE
#define CFG_TUSB_DEBUG           0
/* A failed assertion returns false instead of executing ebreak (ch32rv_port.c). */
#define CFG_TUSB_DEBUG_BREAKPOINT ch32rv_tusb_breakpoint

#define CFG_TUD_ENABLED          CH32RV_TINYUSB_DEVICE
#define CFG_TUH_ENABLED          CH32RV_TINYUSB_HOST
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

/* ---- host ---- */
/* Like the device: the class drivers come from the CH32RVUsbHost class
 * objects (usbh_app_driver_get_cb), so a sketch links only what it declares
 * (CFG_TUH_APP_DRIVERS_ONLY, the matching patch to host/usbh.c). The counts
 * below size TinyUSB's tables. A hub is supported: one level, the devices
 * behind it share the full-speed bus. */
#define CFG_TUH_APP_DRIVERS_ONLY 1
#ifndef CFG_TUH_HUB
#define CFG_TUH_HUB              1
#endif
#ifndef CFG_TUH_DEVICE_MAX
#define CFG_TUH_DEVICE_MAX       (CFG_TUH_HUB ? 4 : 1)
#endif
#ifndef CFG_TUH_ENUMERATION_BUFSIZE
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#endif
#ifndef CFG_TUH_INTERFACE_MAX
#define CFG_TUH_INTERFACE_MAX    8
#endif
#ifndef CFG_TUH_CDC
#define CFG_TUH_CDC              2
#endif
/* The USB-serial chips TinyUSB's CDC host also speaks. */
#ifndef CFG_TUH_CDC_FTDI
#define CFG_TUH_CDC_FTDI         1
#endif
#ifndef CFG_TUH_CDC_CP210X
#define CFG_TUH_CDC_CP210X       1
#endif
#ifndef CFG_TUH_CDC_CH34X
#define CFG_TUH_CDC_CH34X        1
#endif
#ifndef CFG_TUH_CDC_PL2303
#define CFG_TUH_CDC_PL2303       1
#endif
/* What a newly mounted port gets before the sketch says otherwise: DTR and RTS
 * on (most CDC devices only talk once DTR is set) and 115200 8N1. */
#ifndef CFG_TUH_CDC_LINE_CONTROL_ON_ENUM
#define CFG_TUH_CDC_LINE_CONTROL_ON_ENUM (CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS)
#endif
#ifndef CFG_TUH_CDC_LINE_CODING_ON_ENUM
#define CFG_TUH_CDC_LINE_CODING_ON_ENUM { 115200, CDC_LINE_CODING_STOP_BITS_1, CDC_LINE_CODING_PARITY_NONE, 8 }
#endif
#ifndef CFG_TUH_CDC_RX_BUFSIZE
#define CFG_TUH_CDC_RX_BUFSIZE   128
#endif
#ifndef CFG_TUH_CDC_TX_BUFSIZE
#define CFG_TUH_CDC_TX_BUFSIZE   128
#endif
#ifndef CFG_TUH_HID
#define CFG_TUH_HID              4
#endif
#ifndef CFG_TUH_HID_EPIN_BUFSIZE
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#endif
#ifndef CFG_TUH_HID_EPOUT_BUFSIZE
#define CFG_TUH_HID_EPOUT_BUFSIZE 64
#endif
#ifndef CFG_TUH_MSC
#define CFG_TUH_MSC              1
#endif
#ifndef CFG_TUH_MIDI
#define CFG_TUH_MIDI             1
#endif
#ifndef CFG_TUH_API_EDPT_XFER
#define CFG_TUH_API_EDPT_XFER    1   /* vendor / raw endpoints */
#endif

/* ---- video ---- */
/* USB Video Class (CH32RVUsbDeviceVideo): one camera with one stream. */
#ifndef CFG_TUD_VIDEO
#define CFG_TUD_VIDEO            1
#endif
#ifndef CFG_TUD_VIDEO_STREAMING
#define CFG_TUD_VIDEO_STREAMING  1
#endif
/* Bulk streaming by default. Isochronous on USBFS (X035/X033) is one
 * 64-byte packet per 1 ms - about 62 KB/s of picture - while bulk takes what
 * the bus has left and the host retries lost packets. 0 here (build_opt.h:
 * -DCFG_TUD_VIDEO_STREAMING_BULK=0) gives a classic isochronous camera. Read
 * only by CH32RVUsbDeviceVideo's descriptor; TinyUSB follows the descriptor. */
#ifndef CFG_TUD_VIDEO_STREAMING_BULK
#define CFG_TUD_VIDEO_STREAMING_BULK 1
#endif
/* The payload buffer: one UVC payload (2-byte header + picture) is assembled
 * here, and it caps the dwMaxPayloadTransferSize committed to the host.
 * Isochronous needs it no larger than the endpoint - 64 on USBFS (X035/X033),
 * up to 1023 on a high-speed controller. Bulk accepts more, but a payload
 * bigger than one packet needs frame lengths whose last payload is not a
 * whole number of packets (CH32RVUsbDeviceVideo::sendFrame() refuses the
 * rest), so the default is one packet: 64 at full speed, 512 bulk / 1023
 * isochronous at high speed. */
#ifndef CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE
#if CFG_TUD_MAX_SPEED == OPT_MODE_HIGH_SPEED
#if CFG_TUD_VIDEO_STREAMING_BULK
#define CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE 512
#else
#define CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE 1023
#endif
#else
#define CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE 64
#endif
#endif
/* ---- end video ---- */

/* ---- audio ---- */
/* USB Audio, UAC1 / UAC2 (CH32RVUsbAudioFunction): one function with a
 * playback (OUT) and a capture (IN) isochronous endpoint, and UAC2's
 * feedback endpoint. Like every class here it is compiled but linked only
 * when a sketch declares the function; until then these buffers take no RAM.
 * Declared with the defaults below they take about 0.8 KB.
 *
 * Packet sizes are per (micro)frame. Full speed carries one packet per 1 ms
 * frame, and the USBFS of CH32X035/X033 caps every endpoint, isochronous ones
 * included, at 64 bytes: 64 bytes a millisecond is 16-bit mono up to 32 kHz
 * or 16-bit stereo up to 16 kHz (CH32RVUsbAudioFunction refuses a format
 * that does not fit). High speed sends one packet per 125 us microframe, so a
 * high-speed controller (USBHS) takes 48 kHz 16-bit stereo; a high-speed
 * build defaults to 196 bytes, what 48 kHz stereo needs if the host runs the
 * port at full speed. On another full-speed controller build_opt.h may raise
 * them, e.g. for 48 kHz 16-bit stereo:
 *     -DCFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX=196
 *     -DCFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX=196
 * The SW_BUF sizes are the FIFOs between the endpoints and read() / write():
 * four packets (4 ms at full speed) by default. */
#ifndef CFG_TUD_AUDIO
#define CFG_TUD_AUDIO            1
#endif
#ifndef CFG_TUD_AUDIO_ENABLE_EP_IN
#define CFG_TUD_AUDIO_ENABLE_EP_IN 1
#endif
#ifndef CFG_TUD_AUDIO_ENABLE_EP_OUT
#define CFG_TUD_AUDIO_ENABLE_EP_OUT 1
#endif
#ifndef CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP 1
#endif
#ifndef CFG_TUD_AUDIO_ENABLE_INTERRUPT_EP
#define CFG_TUD_AUDIO_ENABLE_INTERRUPT_EP 0
#endif
#ifndef CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX
#if CFG_TUD_MAX_SPEED == OPT_MODE_HIGH_SPEED
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX 196
#else
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX 64
#endif
#endif
#ifndef CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX
#if CFG_TUD_MAX_SPEED == OPT_MODE_HIGH_SPEED
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX 196
#else
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX 64
#endif
#endif
#ifndef CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ (4 * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX)
#endif
#ifndef CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ (4 * CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX)
#endif
/* The largest control answer: a UAC2 sample-rate RANGE (2 + 12 bytes per
 * rate; one rate here). 64 shares EP0's buffer instead of taking its own. */
#ifndef CFG_TUD_AUDIO_CTRL_BUF_SZ
#define CFG_TUD_AUDIO_CTRL_BUF_SZ 64
#endif
#if (defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)) && \
    (CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX > 64 || CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX > 64)
#error "CH32X035/X033 USBFS endpoints hold 64 bytes: CFG_TUD_AUDIO_FUNC_1_EP_*_SZ_MAX must not exceed 64"
#endif
/* ---- end audio ---- */

#ifdef __cplusplus
}
#endif
