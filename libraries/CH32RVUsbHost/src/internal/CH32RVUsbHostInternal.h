/* What the function groups of CH32RVUsbHost share: the host stack
 * (CH32RVUsbHostCore.cpp) as they see it. Not for sketches. */
#pragma once

#include "CH32RVUsbHost.h"
#include "internal/CH32RVUsbHcd.h"

/* CH32RV_USB_HOST_STACK: the series with a host controller driver (CH32RVUsbHostPort.h). */

/* A SuperSpeed host (CH32RVUsbHcdUsbss.cpp): the stack's SuperSpeed
 * enumeration, hub and route code is built only where one exists, and linked
 * only when a sketch picks SUPER_SPEED (ch32rv_usb_host_windows.ss below). */
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416)
#define CH32RV_USB_HOST_SUPERSPEED 1
#else
#define CH32RV_USB_HOST_SUPERSPEED 0
#endif

/* Device addresses 1..CH32RV_USB_HOST_DEVICES: a hub and the devices behind it. */
#define CH32RV_USB_HOST_DEVICES 5
/* A window of a configuration descriptor (the enumeration's and the walks'
 * below), and the scratch buffer the walks share (a HID report descriptor is
 * read into it whole, up to its size). A configuration may be up to 64 KB; it
 * is read a window at a time, the window starting on a packet of EP0, so it
 * must hold more than one: 256 bytes (EP0 is 64 at most), 1024 with
 * SuperSpeed's 512-byte EP0.
 *
 * ch32rv_usb_host_windows holds them and the SuperSpeed state (route strings,
 * hub tiers and delays, U1 / U2 exit latencies; nullptr: no SuperSpeed). The
 * stack's definition is weak, with the 256-byte windows; the USBSS driver's,
 * linked only when a sketch picks SUPER_SPEED, takes its place with 1024-byte
 * ones and the state. LTO then sees `ss` as nullptr on every other port and
 * drops the SuperSpeed code. */
#if CH32RV_USB_HOST_SUPERSPEED
struct CH32RVUsbHostSsDevice {
    uint32_t route;                /* the route string (USB 3.2 8.9) */
    uint8_t tier;                  /* 0 on the root port; a hub's SET_HUB_DEPTH value */
    uint8_t u1Latency;             /* the BOS's U1 / U2 exit latencies (us) */
    uint16_t hubDelay;             /* the hubs' delay to it (ns, SET_ISOCH_DELAY) */
    uint16_t u2Latency;
};
struct CH32RVUsbHostSsState {
    CH32RVUsbHostSsDevice devs[CH32RV_USB_HOST_DEVICES + 1];   /* by address, as the stack's devices */
    /* The enumeration's: the device being enumerated, the next E_LPM request,
     * a hub port's warm reset asked for, SET_SEL's data. */
    CH32RVUsbHostSsDevice now;
    uint8_t lpm;
    bool warmed;
    uint8_t sel[6] __attribute__((aligned(4)));
};
#else
struct CH32RVUsbHostSsState;
#endif
struct CH32RVUsbHostWindows {
    uint8_t *scratch;
    uint8_t *enumeration;
    uint16_t size;                 /* each window's bytes */
    CH32RVUsbHostSsState *ss;
};
extern const CH32RVUsbHostWindows ch32rv_usb_host_windows;
#define CH32RV_USB_HOST_SCRATCH (ch32rv_usb_host_windows.size)
#define ch32rv_usb_host_scratch (ch32rv_usb_host_windows.scratch)

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
    /* From deliverEvents(), never inside the task: run the sketch's callbacks
     * for what poll() kept; how many ran (nullptr: the group has none). */
    size_t (*deliver)(CH32RVUsbHost &host);
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

/* The shared scratch buffer (a configuration's window, a string or report
 * descriptor). One user at a time, from task context. */

/* A buffer of at least `need` bytes: `own` when its `ownSize` is enough, else
 * one from the controller's heap (CH32RVUsbHcd::bufferAlloc) kept in `heap`,
 * any earlier one there freed first; nullptr when neither will do. Only a
 * controller whose packets outgrow the static buffers allocates (V30x's
 * first-generation USBHS), so a full-speed sketch carries no more than before.
 * ch32rv_usb_host_buffer_free() gives `heap` back. */
uint8_t *ch32rv_usb_host_buffer(uint8_t *own, uint16_t ownSize, uint16_t need, uint8_t *&heap);
void ch32rv_usb_host_buffer_free(uint8_t *&heap);

/* Walk the active configuration descriptor of `daddr` a descriptor at a time,
 * however long it is: the scratch buffer holds a window of it, read again
 * further on when the next descriptor is not whole in it. Each descriptor
 * returned is whole, and so is the one after it unless the two do not fit
 * a window (an endpoint's SuperSpeed companion is there). A pointer is good
 * until the next call. nullptr at the end, on a malformed descriptor or a
 * failed transfer.
 *
 *     CH32RVUsbHostConfigWalk w;
 *     for (const uint8_t *d = ch32rv_usb_host_config_first(w, daddr); d; d = ch32rv_usb_host_config_next(w)) ... */
struct CH32RVUsbHostConfigWalk {
    uint8_t daddr;
    uint16_t total;                /* wTotalLength */
    uint16_t base, length;         /* the window: from `base` in the configuration, `length` bytes */
    uint16_t at;                   /* the current descriptor's offset in the configuration */
};
const uint8_t *ch32rv_usb_host_config_first(CH32RVUsbHostConfigWalk &w, uint8_t daddr);
const uint8_t *ch32rv_usb_host_config_next(CH32RVUsbHostConfigWalk &w);
/* Bytes from the current descriptor to the end of the window. */
uint16_t ch32rv_usb_host_config_room(const CH32RVUsbHostConfigWalk &w);

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
/* Suspend (true) or resume the root port's link: U3 on SuperSpeed. false
 * where the controller cannot (no public API yet). */
bool ch32rv_usb_host_suspend(bool suspend);

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
    uint8_t *in;                   /* inBuf, or a heap one for a packet larger than it */
    uint8_t *heapIn, *heapRing;    /* ch32rv_usb_host_buffer()'s */
    uint8_t inBuf[CH32RV_USB_HOST_MAX_PACKET] __attribute__((aligned(4)));
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
