/* What the function groups of CH32RVUsbHost share: the host stack
 * (CH32RVUsbHostCore.cpp) as they see it. Not for sketches. */
#pragma once

#include "CH32RVUsbHost.h"
#include "CH32RVUsbHcd.h"

/* The series with a host controller driver (CH32RVUsbHostPort.h). */
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033) || defined(CH32RV_SERIES_CH32V203) || \
    defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V303) || defined(CH32RV_SERIES_CH32V305) ||  \
    defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317) || defined(CH32RV_SERIES_CH32V205) ||  \
    defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103) || defined(CH32RV_SERIES_CH32M030) ||  \
    defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
#define CH32RV_USB_HOST_STACK 1
#else
#define CH32RV_USB_HOST_STACK 0
#endif

/* Device addresses 1..CH32RV_USB_HOST_DEVICES: a hub and the devices behind it. */
#define CH32RV_USB_HOST_DEVICES 5
/* The configuration descriptor buffer (enumeration and the descriptor walks). */
#define CH32RV_USB_HOST_SCRATCH 256

/* A transfer finished: result CH32RV_USBH_*, length what moved. Runs in
 * normal context, from the stack; it may start the next transfer. */
typedef void (*CH32RVUsbHostXferDone)(void *user, uint8_t result, uint16_t length);

/* A function group (HID, CDC, ...) joins at static-initialisation time, from
 * the file that implements it - so it exists exactly when the sketch calls
 * something in that file. */
struct CH32RVUsbHostGroup {
    /* Offered each interface of a newly configured device (the descriptor at
     * `itf`, `length` bytes to the end of the configuration). Take it by
     * opening its endpoints and returning the bytes it spans (interface,
     * class and endpoint descriptors; an association's interfaces too), or
     * return 0. nullptr: the group takes nothing by itself (vendor, CCID). */
    uint16_t (*open)(uint8_t daddr, const uint8_t *itf, uint16_t length);
    /* The configuration is set: start what the interface needs (class
     * requests may block here). */
    void (*configured)(uint8_t daddr, uint8_t itf);
    void (*poll)(CH32RVUsbHost &host);   /* after every task run (nullptr: nothing) */
    void (*detach)(uint8_t daddr);       /* a device went away (nullptr: nothing) */
    CH32RVUsbHostGroup *next;
};
void ch32rv_usb_host_join(CH32RVUsbHostGroup &group);

/* Interfaces a group took, so getInterfaces() can say "claimed". */
void ch32rv_usb_host_mark_claimed(uint8_t daddr, uint8_t itf);

/* Run the stack's events once (completions, the root port, enumeration);
 * what the waits below do. */
void ch32rv_usb_host_pump(void);
/* Run the stack's events until *done, or false (a timeout) after timeoutMs. */
bool ch32rv_usb_host_wait(volatile bool &done, uint32_t timeoutMs);

/* The active configuration descriptor of `daddr` into the shared scratch
 * buffer; its length, 0 on failure. One user at a time, from task context. */
extern uint8_t ch32rv_usb_host_scratch[CH32RV_USB_HOST_SCRATCH];
uint16_t ch32rv_usb_host_config_descriptor(uint8_t daddr);

/* The device record, or nullptr if `daddr` is not a known device. */
const CH32RVUsbHostDeviceInfo *ch32rv_usb_host_device(uint8_t daddr);
bool ch32rv_usb_host_connected(uint8_t daddr);
/* Its device descriptor (18 bytes), from enumeration on: the groups'
 * open() can tell a USB serial chip by VID and PID. */
const uint8_t *ch32rv_usb_host_device_descriptor(uint8_t daddr);

/* A synchronous control transfer with a timeout; sets host.lastError(). */
bool ch32rv_usb_host_control(CH32RVUsbHost &host, uint8_t daddr, uint8_t requestType, uint8_t request,
                             uint16_t value, uint16_t index, uint8_t *data, uint16_t length,
                             size_t *actualLength, uint32_t timeoutMs);
/* The same without a host object (no lastError); the result as CH32RV_USBH_*. */
uint8_t ch32rv_usb_host_control_sync(uint8_t daddr, uint8_t requestType, uint8_t request, uint16_t value,
                                     uint16_t index, uint8_t *data, uint16_t length, uint16_t *actualLength,
                                     uint32_t timeoutMs);

/* Endpoints of a device: open from an endpoint descriptor, transfer (one at a
 * time per endpoint; false while one is pending), abort (no completion). */
bool ch32rv_usb_host_ep_open(uint8_t daddr, const uint8_t *endpointDescriptor);
void ch32rv_usb_host_ep_close(uint8_t daddr, uint8_t address);
bool ch32rv_usb_host_xfer(uint8_t daddr, uint8_t address, uint8_t *buffer, uint16_t length,
                          CH32RVUsbHostXferDone done, void *user);
bool ch32rv_usb_host_xfer_busy(uint8_t daddr, uint8_t address);
/* A transfer and the wait for it; on a timeout it is aborted before this
 * returns, so the buffer is the caller's again. The result as CH32RV_USBH_*. */
uint8_t ch32rv_usb_host_xfer_sync(uint8_t daddr, uint8_t address, uint8_t *buffer, uint16_t length,
                                  uint16_t *actualLength, uint32_t timeoutMs);
void ch32rv_usb_host_abort(uint8_t daddr, uint8_t address);
/* CLEAR_FEATURE(ENDPOINT_HALT) and the toggle back to DATA0; blocking. */
bool ch32rv_usb_host_clear_halt(uint8_t daddr, uint8_t address);
/* Enumerate the device on the root port again (a stuck device's way back). */
void ch32rv_usb_host_reenumerate(uint8_t daddr);

/* Map a transfer result to the library's error. */
CH32RVUsbHostError ch32rv_usb_host_result_error(uint8_t result);

/* The smaller of two (TinyUSB's TU_MIN). */
template <typename T> static inline T ch32rv_min(T a, T b) { return a < b ? a : b; }

/* Walk a configuration descriptor. */
static inline const uint8_t *ch32rv_usb_desc_next(const uint8_t *d) { return d + d[0]; }

/* A bulk pipe pair on an interface no class driver took (vendor, CCID): IN is
 * kept armed into a ring, OUT is written synchronously. */
struct CH32RVUsbHostPipe {
    bool open;
    uint8_t daddr, itf;
    uint8_t epIn, epOut, epInt;
    uint16_t mpsIn, mpsOut;
    volatile bool inBusy;
    volatile bool outDone;
    volatile uint8_t outResult;
    uint8_t *ring;
    uint16_t ringSize;
    volatile uint16_t head, tail;
    uint8_t inBuf[64] __attribute__((aligned(4)));
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
