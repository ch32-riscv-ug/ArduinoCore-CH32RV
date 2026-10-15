/* What the function groups of CH32RVUsbHost share. Not for sketches. */
#pragma once

#include "CH32RVUsbHost.h"

#include "tusb.h"
#if CH32RV_TINYUSB_HOST
#include "host/usbh_pvt.h"
#else
typedef struct usbh_class_driver_s usbh_class_driver_t;   /* never defined: no host here */
#endif

/* A function group (HID, CDC, ...) joins at static-initialisation time, from
 * the file that implements it - so it exists exactly when the sketch calls
 * something in that file: its TinyUSB class driver (or none, for the groups
 * that open endpoints themselves) and a poll run after every tuh_task(). */
struct CH32RVUsbHostGroup {
    const usbh_class_driver_t *driver;   /* nullptr: no class driver */
    void (*poll)(CH32RVUsbHost &host);   /* nullptr: nothing to do */
    void (*detach)(uint8_t daddr);       /* a device went away (nullptr: nothing) */
    CH32RVUsbHostGroup *next;
};
void ch32rv_usb_host_join(CH32RVUsbHostGroup &group);

/* Interfaces a group took, so getInterfaces() can say "claimed". */
void ch32rv_usb_host_mark_claimed(uint8_t daddr, uint8_t itf);

/* Wait for *done while driving the stack; false (Timeout) after timeoutMs. */
bool ch32rv_usb_host_wait(volatile bool &done, uint32_t timeoutMs);

/* The active configuration descriptor of `daddr` into the shared scratch
 * buffer; its length, 0 on failure. One user at a time, from task context. */
extern uint8_t ch32rv_usb_host_scratch[CFG_TUH_ENUMERATION_BUFSIZE];
uint16_t ch32rv_usb_host_config_descriptor(uint8_t daddr);

/* The device record, or nullptr if `daddr` is not a known device. */
const CH32RVUsbHostDeviceInfo *ch32rv_usb_host_device(uint8_t daddr);

/* A synchronous control transfer with a timeout; sets host.lastError(). */
bool ch32rv_usb_host_control(CH32RVUsbHost &host, uint8_t daddr, uint8_t requestType, uint8_t request,
                             uint16_t value, uint16_t index, uint8_t *data, uint16_t length,
                             size_t *actualLength, uint32_t timeoutMs);

/* The host controller driver's way back from a stuck class driver: the root
 * port's device enumerates again (ch32rv_hcd_usbfs.c). */
extern "C" void ch32rv_hcd_usbfs_reenumerate(uint8_t rhport);

/* Map a TinyUSB transfer result to the library's error. */
CH32RVUsbHostError ch32rv_usb_host_result_error(xfer_result_t result);

/* A bulk pipe pair on an interface no class driver took (vendor, CCID): IN is
 * kept armed into a ring, OUT is written synchronously. */
struct CH32RVUsbHostPipe {
    bool open;
    uint8_t daddr, itf;
    uint8_t epIn, epOut, epInt;
    uint16_t mpsIn, mpsOut;
    volatile bool inBusy;
    volatile bool outDone;
    volatile xfer_result_t outResult;
    uint8_t *ring;
    uint16_t ringSize;
    volatile uint16_t head, tail;
    TU_ATTR_ALIGNED(4) uint8_t inBuf[64];
};

/* Find interface `itf` (0xFF: the first of `cls`) on `daddr`, open its bulk
 * endpoints and start receiving. */
bool ch32rv_usb_host_pipe_open(CH32RVUsbHostPipe &p, uint8_t daddr, uint8_t itf, uint8_t cls,
                               uint8_t *ring, uint16_t ringSize);
void ch32rv_usb_host_pipe_close(CH32RVUsbHostPipe &p);
/* Re-arm the IN transfer once the ring has room; call from the group's poll. */
void ch32rv_usb_host_pipe_poll(CH32RVUsbHostPipe &p);
uint16_t ch32rv_usb_host_pipe_available(const CH32RVUsbHostPipe &p);
uint16_t ch32rv_usb_host_pipe_read(CH32RVUsbHostPipe &p, uint8_t *out, uint16_t length);
bool ch32rv_usb_host_pipe_write(CH32RVUsbHost &host, CH32RVUsbHostPipe &p, const uint8_t *data, size_t length,
                                uint32_t timeoutMs);
