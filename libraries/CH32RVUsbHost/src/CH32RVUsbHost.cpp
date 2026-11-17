/* CH32RVUsbHost: the host stack - lifecycle, the device table, enumeration
 * (root port and hubs), control transfers, the endpoint API the function
 * groups use - and printing. The groups (HID, CDC, ...) are in files of their
 * own; the controller driver behind CH32RVUsbHcd.h.
 *
 * The controller's interrupt only records: a transfer finished, the root port
 * changed. Everything else - the control stages, enumeration, the classes'
 * completions - runs in normal context from ch32rv_usb_host_task() (after
 * loop(), inside delay()) and from the blocking calls while they wait. */
#include "internal/CH32RVUsbHostInternal.h"
#include "internal/ch32rv_deliver.h"
#include "internal/ch32rv_service.h"

#include <string.h>

#if CH32RV_USB_HOST_STACK

namespace {

const CH32RVUsbHcd *hcd;

/* The SuperSpeed state: nullptr (and the code that uses it dropped) where no
 * SuperSpeed host is linked (CH32RVUsbHostInternal.h). */
#if CH32RV_USB_HOST_SUPERSPEED
#define SS_ON  (ch32rv_usb_host_windows.ss != nullptr)
#define SS     (*ch32rv_usb_host_windows.ss)
#define SSD(d) (SS.devs[&(d) - devs])
#else
#define SS_ON false
#endif
#define en_buf (ch32rv_usb_host_windows.enumeration)

/* A SuperSpeed device; false where no SuperSpeed host is linked. */
inline bool is_super(uint8_t speed)
{
    return SS_ON && speed == CH32RV_USBH_SUPER;
}

/* ------------------------------------------------------------- events */
struct Event {
    uint8_t daddr, address, result;
    uint16_t length;
};
#define EVENTS 32
volatile Event events[EVENTS];
volatile uint8_t ev_head, ev_tail;
volatile bool port_changed;
/* Bus power of the root port: suspended by suspend(), the driver's suspend
 * (its _power table, found by the first suspend() after begin()), a remote
 * wakeup seen by the driver's interrupt, wakeups not yet handed to
 * onRemoteWakeup(), and the hooks suspend() puts into the task and the
 * delivery - so none of it is linked into a sketch that never suspends. */
bool root_suspended;
const CH32RVUsbHcdPower *power;
volatile bool root_wakeup;
uint8_t wakeups_due;
CH32RVUsbHost::RemoteWakeupCallback remote_wakeup_cb;
void (*power_pump)(void);
size_t (*power_deliver)(CH32RVUsbHost &host);

/* A transfer in flight on an endpoint (not EP0) and who hears of it. */
struct Pending {
    uint8_t daddr, address;
    CH32RVUsbHostXferDone done;
    void *user;
};
#define PENDING 24
Pending pending[PENDING];

/* ------------------------------------------------------------- devices */
enum : uint8_t { DEV_FREE, DEV_ENUM, DEV_MOUNTED };

struct Device {
    uint8_t state;
    uint8_t parent, port, speed;   /* parent 0: the root port */
    uint16_t mps0;
    uint8_t desc[18];
    uint8_t configValue, configAttributes, configPower, configInterfaces;
    uint16_t configLength;
    uint16_t claimed;              /* interface bitmap */
    CH32RVUsbHostGroup *owner[16];
    bool configurePending;         /* the groups' configured() still due */
    bool infoPending;              /* the strings and onDeviceConnected() still due */
    bool known;                    /* info filled, onDeviceConnected() called */
    CH32RVUsbHostDeviceInfo info;
    /* Manufacturer, product and serial number in the string pool: offset
     * and bytes with the terminator (0: none). */
    uint16_t stringAt[3];
    uint8_t stringBytes[3];
};
Device devs[CH32RV_USB_HOST_DEVICES + 1];   /* by address; 0 is the one being enumerated */

/* ------------------------------------------------------------- strings
 * The devices' strings, kept whole (a descriptor carries up to 126
 * characters) in one pool: each takes the bytes it needs, first fit, freed
 * with its device. The pool holds one device's three longest possible
 * strings and the usual short ones of the others; when it is full a string
 * is cut to the room left. */
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415) || \
    defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315) ||   \
    defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
#define STRING_POOL 1024
#else
#define STRING_POOL 512
#endif
char string_pool[STRING_POOL];

/* The lowest offset with `bytes` free, or the start of the largest gap
 * (*bytes cut to it); false when the pool is full. */
bool string_alloc(uint16_t *at, uint16_t *bytes)
{
    uint16_t best = 0, bestRoom = 0;
    uint16_t from = 0;
    for (;;) {
        /* The first string at or after `from`, and the gap before it. */
        uint16_t next = STRING_POOL, end = from;
        for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
            for (uint8_t k = 0; k < 3; k++) {
                const uint16_t s = devs[a].stringAt[k], n = devs[a].stringBytes[k];
                if (n && s + n > from && s < next) {
                    next = s < from ? from : s;
                    end = (uint16_t)(s + n);
                }
            }
        }
        const uint16_t room = (uint16_t)(next - from);
        if (room >= *bytes) {
            *at = from;
            return true;
        }
        if (room > bestRoom) {
            best = from;
            bestRoom = room;
        }
        if (next >= STRING_POOL) {
            break;
        }
        from = end;
    }
    if (bestRoom < 2) {
        return false;
    }
    *at = best;
    *bytes = bestRoom;
    return true;
}

const char *string_of(const Device &d, uint8_t k)
{
    return d.stringBytes[k] ? string_pool + d.stringAt[k] : "";
}

CH32RVUsbHostGroup *groups;

/* onDeviceConnected() / onDeviceDisconnected(), in order, for
 * deliverEvents(); a copy of the info, since the device may be gone (or its
 * address taken again) by then. */
struct DeviceEvent {
    bool connected;
    CH32RVUsbHostDeviceInfo info;
};
#define DEVICE_EVENTS 4
DeviceEvent device_events[DEVICE_EVENTS];
uint8_t device_first, device_count;
uint32_t device_dropped;

void device_event(bool connected, const CH32RVUsbHostDeviceInfo &info)
{
    if (device_count == DEVICE_EVENTS) {
        device_dropped++;
        return;
    }
    DeviceEvent &e = device_events[(uint8_t)(device_first + device_count) % DEVICE_EVENTS];
    e.connected = connected;
    e.info = info;
    if (!connected) {
        e.info.manufacturer = e.info.product = e.info.serial = "";   /* their storage is reused */
    }
    device_count++;
}

/* ------------------------------------------------------------- control */
enum : uint8_t { CTL_IDLE, CTL_SETUP, CTL_SKIP, CTL_DATA, CTL_STATUS };
struct Control {
    uint8_t stage;
    uint8_t daddr;
    uint8_t setup[8];
    uint8_t *buf;
    uint16_t len, actual;
    /* An IN data stage read from `skip` on: the bytes before it are taken
     * into `buf` and dropped, whole packets at a time (skip is a multiple of
     * EP0's packet), so a descriptor longer than the buffer can be read a
     * window at a time. */
    uint16_t skip, skipped, piece;
    CH32RVUsbHostXferDone done;
    void *user;
} ctl;

void control_finish(uint8_t result)
{
    const CH32RVUsbHostXferDone done = ctl.done;
    void *user = ctl.user;
    const uint16_t actual = ctl.actual;
    ctl.stage = CTL_IDLE;
    if (done) {
        done(user, result, actual);
    }
}

void control_status(void)
{
    ctl.stage = CTL_STATUS;
    /* The status stage goes the other way: IN after OUT or no data. */
    const uint8_t address = (ctl.setup[0] & 0x80) && ctl.len ? 0x00 : 0x80;
    if (!hcd->xfer(ctl.daddr, address, nullptr, 0)) {
        control_finish(CH32RV_USBH_FAILED);
    }
}

/* After the setup stage, or a piece dropped: the next piece to drop, the
 * data stage, or the status stage. */
void control_data(void)
{
    if (ctl.skipped < ctl.skip) {
        ctl.stage = CTL_SKIP;
        const uint16_t mps = devs[ctl.daddr].mps0;
        const uint16_t most = (uint16_t)(ctl.len / mps * mps), left = (uint16_t)(ctl.skip - ctl.skipped);
        ctl.piece = left < most ? left : most;
        if (!hcd->xfer(ctl.daddr, 0x80, ctl.buf, ctl.piece)) {
            control_finish(CH32RV_USBH_FAILED);
        }
    } else if (ctl.len) {
        ctl.stage = CTL_DATA;
        if (!hcd->xfer(ctl.daddr, (ctl.setup[0] & 0x80) ? 0x80 : 0x00, ctl.buf, ctl.len)) {
            control_finish(CH32RV_USBH_FAILED);
        }
    } else {
        control_status();
    }
}

void control_event(uint8_t result, uint16_t length)
{
    if (result != CH32RV_USBH_OK) {
        control_finish(result);
        return;
    }
    switch (ctl.stage) {
    case CTL_SKIP:
        if (length < ctl.piece) {
            ctl.actual = 0;            /* it ended before the window */
            control_status();
            break;
        }
        ctl.skipped = (uint16_t)(ctl.skipped + length);
        control_data();
        break;
    case CTL_SETUP:
        control_data();
        break;
    case CTL_DATA:
        ctl.actual = length;
        control_status();
        break;
    case CTL_STATUS:
        if (!(ctl.setup[0] & 0x80) || !ctl.len) {
            ctl.actual = ctl.len;
        }
        control_finish(CH32RV_USBH_OK);
        break;
    }
}

/* `skip` (IN only): the data stage's first bytes to drop, a multiple of
 * EP0's packet and of no more than `length` bytes a piece; wLength asks for
 * them as well as `length`. */
bool control_start(uint8_t daddr, uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                   uint8_t *data, uint16_t length, CH32RVUsbHostXferDone done, void *user, uint16_t skip = 0)
{
    if (ctl.stage != CTL_IDLE) {
        return false;
    }
    if (skip) {
        const uint16_t mps = devs[daddr].mps0;
        if (!(requestType & 0x80) || !mps || length < mps || skip % mps || (uint32_t)skip + length > 0xFFFFu) {
            return false;
        }
    }
    const uint16_t total = (uint16_t)(skip + length);
    const uint8_t s[8] = {requestType, request, (uint8_t)value, (uint8_t)(value >> 8),
                          (uint8_t)index, (uint8_t)(index >> 8), (uint8_t)total, (uint8_t)(total >> 8)};
    memcpy(ctl.setup, s, 8);
    ctl.daddr = daddr;
    ctl.buf = data;
    ctl.len = length;
    ctl.actual = 0;
    ctl.skip = skip;
    ctl.skipped = 0;
    ctl.done = done;
    ctl.user = user;
    ctl.stage = CTL_SETUP;
    if (!hcd->setup(daddr, ctl.setup)) {
        ctl.stage = CTL_IDLE;
        return false;
    }
    return true;
}

void control_abort(void)
{
    if (ctl.stage != CTL_IDLE) {
        hcd->abort(ctl.daddr, 0x00);
        ctl.stage = CTL_IDLE;
    }
}

/* ------------------------------------------------------------- dispatch */
void pending_drop(uint8_t daddr, uint8_t address)
{
    for (Pending &p : pending) {
        if (p.done && p.daddr == daddr && (address == 0xFF || p.address == address)) {
            p.done = nullptr;
        }
    }
}

/* Completions already queued for an endpoint that was aborted or closed must
 * not reach the transfer started on it next. */
void events_void(uint8_t daddr, uint8_t address)
{
    noInterrupts();
    for (uint8_t i = ev_tail; i != ev_head; i = (uint8_t)((i + 1) % EVENTS)) {
        if (events[i].daddr == daddr && (address == 0xFF || events[i].address == address)) {
            events[i].daddr = 0xFF;
        }
    }
    interrupts();
}

void dispatch(uint8_t daddr, uint8_t address, uint8_t result, uint16_t length)
{
    if (daddr == 0xFF) {
        return;
    }
    if ((address & 0x0F) == 0) {
        if (ctl.stage != CTL_IDLE && daddr == ctl.daddr) {
            control_event(result, length);
        }
        return;
    }
    for (Pending &p : pending) {
        if (p.done && p.daddr == daddr && p.address == address) {
            const CH32RVUsbHostXferDone done = p.done;
            void *user = p.user;
            p.done = nullptr;
            done(user, result, length);
            return;
        }
    }
}

void remove_device(uint8_t daddr);
void pump(void);

}  // namespace

extern "C" void ch32rv_usbh_isr_xfer(uint8_t daddr, uint8_t address, uint8_t result, uint16_t length)
{
    const uint8_t next = (uint8_t)((ev_head + 1) % EVENTS);
    if (next == ev_tail) {
        return;                    /* cannot happen: one per open endpoint at most */
    }
    events[ev_head].daddr = daddr;
    events[ev_head].address = address;
    events[ev_head].result = result;
    events[ev_head].length = length;
    ev_head = next;
}

extern "C" void ch32rv_usbh_isr_port(bool attached)
{
    (void)attached;
    port_changed = true;
}

extern "C" void ch32rv_usbh_isr_wakeup(void)
{
    root_wakeup = true;
}

/* ------------------------------------------------------------- the endpoint API */
CH32RVUsbHost *ch32rv_usb_host_active;
/* The windows without SuperSpeed (weak: the USBSS driver's take their place). */
uint8_t scratch_window[256] __attribute__((aligned(4)));
uint8_t enumeration_window[256] __attribute__((aligned(4)));

void ch32rv_usb_host_buffer_free(uint8_t *&heap)
{
    if (heap && hcd && hcd->bufferFree) {
        hcd->bufferFree(heap);
    }
    heap = nullptr;
}

uint8_t *ch32rv_usb_host_buffer(uint8_t *own, uint16_t ownSize, uint16_t need, uint8_t *&heap)
{
    ch32rv_usb_host_buffer_free(heap);
    if (need <= ownSize) {
        return own;
    }
    if (hcd && hcd->bufferAlloc) {
        heap = (uint8_t *)hcd->bufferAlloc(need);
    }
    return heap;
}

void ch32rv_usb_host_join(CH32RVUsbHostGroup &group)
{
    for (CH32RVUsbHostGroup *g = groups; g; g = g->next) {
        if (g == &group) {
            return;
        }
    }
    /* At the end: the hub (joined first, by this file) is offered interfaces first. */
    group.next = nullptr;
    CH32RVUsbHostGroup **at = &groups;
    while (*at) {
        at = &(*at)->next;
    }
    *at = &group;
}

void ch32rv_usb_host_mark_claimed(uint8_t daddr, uint8_t itf)
{
    if (daddr && daddr <= CH32RV_USB_HOST_DEVICES && itf < 16) {
        devs[daddr].claimed = (uint16_t)(devs[daddr].claimed | (1u << itf));
        devs[daddr].info.supported = true;
    }
}

const CH32RVUsbHostDeviceInfo *ch32rv_usb_host_device(uint8_t daddr)
{
    return daddr && daddr <= CH32RV_USB_HOST_DEVICES && devs[daddr].known ? &devs[daddr].info : nullptr;
}

bool ch32rv_usb_host_connected(uint8_t daddr)
{
    return hcd && daddr && daddr <= CH32RV_USB_HOST_DEVICES && devs[daddr].state == DEV_MOUNTED;
}

const uint8_t *ch32rv_usb_host_device_descriptor(uint8_t daddr)
{
    return daddr && daddr <= CH32RV_USB_HOST_DEVICES && devs[daddr].state != DEV_FREE ? devs[daddr].desc : nullptr;
}

/* The transaction translator a full- or low-speed device on port `port` of
 * hub `hub` (0: the root port) is reached through: the nearest high-speed hub
 * on the way to the root and the port of it the way goes down (USB 2.0
 * 11.14). Left 0 for a high-speed device, one with no high-speed hub above
 * it, and at SuperSpeed. */
static void tt_of(uint8_t hub, uint8_t port, CH32RVUsbHcdEp &e)
{
    if (e.speed != CH32RV_USBH_FULL && e.speed != CH32RV_USBH_LOW) {
        return;
    }
    for (uint8_t tier = 0; hub && hub <= CH32RV_USB_HOST_DEVICES && tier < 7; tier++) {
        if (devs[hub].speed == CH32RV_USBH_HIGH) {
            e.ttHub = hub;
            e.ttPort = port;
            return;
        }
        port = devs[hub].port;
        hub = devs[hub].parent;
    }
}

bool ch32rv_usb_host_ep_open(uint8_t daddr, const uint8_t *d)
{
    if (!hcd || daddr > CH32RV_USB_HOST_DEVICES || devs[daddr].state == DEV_FREE || d[0] < 7 || d[1] != 5) {
        return false;
    }
    CH32RVUsbHcdEp e = {};
    e.address = d[2];
    e.type = (uint8_t)(d[3] & 3);
    e.mps = (uint16_t)((d[4] | d[5] << 8) & 0x7FF);
    e.interval = d[6];
    e.speed = devs[daddr].speed;
    tt_of(devs[daddr].parent, devs[daddr].port, e);
    /* SuperSpeed: the endpoint companion follows (USB 3.2 9.6.7). The
     * callers hand an endpoint descriptor inside a configuration they have
     * bounds-checked; the companion is mandatory there, so it is read
     * without a length of its own. */
    const uint8_t *c = d + d[0];
    if (is_super(e.speed) && c[0] >= 6 && c[1] == 0x30) {
        e.maxBurst = c[2];
        e.attributes = c[3];
        e.bytesPerInterval = (uint16_t)(c[4] | c[5] << 8);
    }
    return hcd->epOpen(daddr, e);
}

/* EP0 of `daddr` at `mps`, a device of `speed` on port `port` of hub `hub`. */
static bool ep0_open(uint8_t daddr, uint16_t mps, uint8_t speed, uint8_t hub, uint8_t port)
{
    CH32RVUsbHcdEp e = {};
    e.type = CH32RV_USBH_CONTROL;
    e.mps = mps;
    e.speed = speed;
    tt_of(hub, port, e);
    return hcd->epOpen(daddr, e);
}

void ch32rv_usb_host_ep_close(uint8_t daddr, uint8_t address)
{
    if (!hcd) {
        return;
    }
    hcd->epClose(daddr, address);
    pending_drop(daddr, address);
    events_void(daddr, address);
}

bool ch32rv_usb_host_xfer(uint8_t daddr, uint8_t address, uint8_t *buffer, uint16_t length,
                          CH32RVUsbHostXferDone done, void *user)
{
    if (!hcd || !done || (address & 0x0F) == 0 || ch32rv_usb_host_xfer_busy(daddr, address)) {
        return false;
    }
    for (Pending &p : pending) {
        if (!p.done) {
            p.daddr = daddr;
            p.address = address;
            p.user = user;
            p.done = done;
            if (!hcd->xfer(daddr, address, buffer, length)) {
                p.done = nullptr;
                return false;
            }
            return true;
        }
    }
    return false;
}

bool ch32rv_usb_host_xfer_busy(uint8_t daddr, uint8_t address)
{
    for (const Pending &p : pending) {
        if (p.done && p.daddr == daddr && p.address == address) {
            return true;
        }
    }
    return false;
}

void ch32rv_usb_host_abort(uint8_t daddr, uint8_t address)
{
    if (!hcd) {
        return;
    }
    hcd->abort(daddr, address);
    pending_drop(daddr, address);
    events_void(daddr, address);
}

namespace {
struct XferWait {
    volatile bool done;
    volatile uint8_t result;
    volatile uint16_t length;
};

void xfer_wait_done(void *user, uint8_t result, uint16_t length)
{
    XferWait &w = *(XferWait *)user;
    w.result = result;
    w.length = length;
    w.done = true;
}
}  // namespace

uint8_t ch32rv_usb_host_xfer_sync(uint8_t daddr, uint8_t address, uint8_t *buffer, uint16_t length,
                                  uint16_t *actualLength, uint32_t timeoutMs)
{
    XferWait w = {false, CH32RV_USBH_FAILED, 0};
    if (actualLength) {
        *actualLength = 0;
    }
    if (!ch32rv_usb_host_xfer(daddr, address, buffer, length, xfer_wait_done, &w)) {
        return CH32RV_USBH_FAILED;
    }
    if (!ch32rv_usb_host_wait(w.done, timeoutMs)) {
        ch32rv_usb_host_abort(daddr, address);   /* w lives on this stack: nothing may reach it later */
        return CH32RV_USBH_TIMEOUT;
    }
    if (actualLength) {
        *actualLength = w.length;
    }
    return w.result;
}

CH32RVUsbHostError ch32rv_usb_host_result_error(uint8_t result)
{
    switch (result) {
    case CH32RV_USBH_OK: return CH32RVUsbHostError::None;
    case CH32RV_USBH_STALLED: return CH32RVUsbHostError::Stalled;
    case CH32RV_USBH_TIMEOUT: return CH32RVUsbHostError::Timeout;
    default: return CH32RVUsbHostError::TransferFailed;
    }
}

/* ------------------------------------------------------------- waiting */
void ch32rv_usb_host_pump(void)
{
    pump();
}

bool ch32rv_usb_host_wait(volatile bool &done, uint32_t timeoutMs)
{
    const uint32_t t0 = millis();
    while (!done) {
        pump();
        if (done) {
            break;
        }
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return false;
        }
    }
    return true;
}

namespace {

/* One synchronous control transfer at a time; static, so a transfer that
 * outlives its caller's timeout still completes into valid memory. */
struct SyncWait {
    volatile bool done;
    volatile uint8_t result;
    volatile uint16_t actual;
    uint32_t generation;
} sync_wait;

void sync_done(void *user, uint8_t result, uint16_t actual)
{
    if ((uint32_t)(uintptr_t)user != sync_wait.generation) {
        return;                    /* a transfer that was given up on */
    }
    sync_wait.result = result;
    sync_wait.actual = actual;
    sync_wait.done = true;
}

}  // namespace

/* ch32rv_usb_host_control_sync() with control_start()'s `skip`. */
static uint8_t control_sync_skip(uint8_t daddr, uint8_t requestType, uint8_t request, uint16_t value,
                                 uint16_t index, uint8_t *data, uint16_t length, uint16_t *actualLength,
                                 uint32_t timeoutMs, uint16_t skip);

uint8_t ch32rv_usb_host_control_sync(uint8_t daddr, uint8_t requestType, uint8_t request, uint16_t value,
                                     uint16_t index, uint8_t *data, uint16_t length, uint16_t *actualLength,
                                     uint32_t timeoutMs)
{
    return control_sync_skip(daddr, requestType, request, value, index, data, length, actualLength, timeoutMs, 0);
}

static uint8_t control_sync_skip(uint8_t daddr, uint8_t requestType, uint8_t request, uint16_t value,
                                 uint16_t index, uint8_t *data, uint16_t length, uint16_t *actualLength,
                                 uint32_t timeoutMs, uint16_t skip)
{
    if (actualLength) {
        *actualLength = 0;
    }
    if (!hcd) {
        return CH32RV_USBH_FAILED;
    }
    const uint32_t t0 = millis();
    /* The control engine serves one transfer at a time (enumeration, a hub,
     * another caller): wait for it. */
    while (ctl.stage != CTL_IDLE) {
        pump();
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return CH32RV_USBH_TIMEOUT;
        }
    }
    sync_wait.generation++;
    sync_wait.done = false;
    if (!control_start(daddr, requestType, request, value, index, data, length, sync_done,
                       (void *)(uintptr_t)sync_wait.generation, skip)) {
        return CH32RV_USBH_FAILED;
    }
    const uint32_t spent = (uint32_t)(millis() - t0);
    if (!ch32rv_usb_host_wait(sync_wait.done, timeoutMs > spent ? timeoutMs - spent : 1)) {
        /* Abandoned: stop it before returning, so its data stage cannot
         * land in the caller's buffer afterwards. */
        sync_wait.generation++;
        if (ctl.done == sync_done) {
            control_abort();
        }
        return CH32RV_USBH_TIMEOUT;
    }
    if (actualLength) {
        *actualLength = sync_wait.actual;
    }
    return sync_wait.result;
}

bool ch32rv_usb_host_control(CH32RVUsbHost &host, uint8_t daddr, uint8_t requestType, uint8_t request,
                             uint16_t value, uint16_t index, uint8_t *data, uint16_t length,
                             size_t *actualLength, uint32_t timeoutMs)
{
    if (actualLength) {
        *actualLength = 0;
    }
    if (!host._started) {
        return host.fail(CH32RVUsbHostError::NotStarted);
    }
    if (!ch32rv_usb_host_connected(daddr)) {
        return host.fail(CH32RVUsbHostError::NotConnected);
    }
    uint16_t actual = 0;
    const uint8_t r = ch32rv_usb_host_control_sync(daddr, requestType, request, value, index, data, length,
                                                   &actual, timeoutMs);
    if (actualLength) {
        *actualLength = actual;
    }
    if (r != CH32RV_USBH_OK) {
        return host.fail(ch32rv_usb_host_result_error(r));
    }
    return host.ok();
}

bool ch32rv_usb_host_clear_halt(uint8_t daddr, uint8_t address)
{
    const uint8_t r = ch32rv_usb_host_control_sync(daddr, 0x02, 1, 0, address, nullptr, 0, nullptr, 1000);
    if (hcd) {
        hcd->resetToggle(daddr, address);
    }
    return r == CH32RV_USBH_OK;
}

/* ------------------------------------------------------------- configuration windows
 * A configuration descriptor may be up to 64 KB long; the stack reads it a
 * window (CH32RV_USB_HOST_SCRATCH bytes) at a time - GET_DESCRIPTOR from
 * the start, the bytes before the window dropped (control_start's skip). */
namespace {

/* The window that holds the configuration from `at` on: it starts at `at`
 * rounded down to EP0's packet (what can be dropped), and holds at least a
 * whole packet. */
void window_for(uint8_t daddr, uint16_t total, uint16_t at, uint16_t *base, uint16_t *length)
{
    const uint16_t mps = devs[daddr].mps0 ? devs[daddr].mps0 : 8;
    uint16_t b = (uint16_t)(at / mps * mps);
    if (b && total - b < mps) {
        b = (uint16_t)(b - mps);       /* the last short packet alone cannot be read past the others */
    }
    const uint16_t left = (uint16_t)(total - b);
    *base = b;
    *length = left < CH32RV_USB_HOST_SCRATCH ? left : (uint16_t)CH32RV_USB_HOST_SCRATCH;
}

/* Whether the window (`buf`, from `base`, `length` bytes) of a configuration
 * of `total` bytes holds the descriptor at `at` whole, and the one after it
 * too unless it is the last (an endpoint's SuperSpeed companion, the next
 * header). A malformed length (under 2) counts as held: the walk stops on it. */
bool window_holds(const uint8_t *buf, uint16_t base, uint16_t length, uint16_t total, uint16_t at)
{
    if (at < base || at - base + 2u > length) {
        return false;
    }
    const uint16_t rel = (uint16_t)(at - base);
    if (buf[rel] < 2) {
        return true;
    }
    const uint32_t end = (uint32_t)rel + buf[rel];
    if (end > length) {
        return false;
    }
    if (base + end + 2u > total) {
        return true;                   /* the last one */
    }
    return end + 2u <= length && (buf[end] < 2 || end + buf[end] <= length);
}

bool window_whole(const uint8_t *buf, uint16_t base, uint16_t length, uint16_t at)
{
    return at >= base && at - base + 2u <= length && at - base + (uint32_t)buf[at - base] <= length;
}

}  // namespace

static bool config_read(CH32RVUsbHostConfigWalk &w, uint16_t at)
{
    uint16_t base, length, got = 0;
    window_for(w.daddr, w.total, at, &base, &length);
    if (control_sync_skip(w.daddr, 0x80, 6, 0x0200, 0, ch32rv_usb_host_scratch, length, &got, 1000, base) !=
        CH32RV_USBH_OK) {
        return false;
    }
    w.base = base;
    w.length = got;
    return got != 0;
}

/* The descriptor at w.at, the window moved to hold it (and the next); one too
 * long for any window is stepped over. nullptr at the end or on a failure. */
static const uint8_t *config_here(CH32RVUsbHostConfigWalk &w)
{
    for (;;) {
        if (w.at + 2u > w.total) {
            return nullptr;
        }
        const uint8_t *buf = ch32rv_usb_host_scratch;
        if (!window_holds(buf, w.base, w.length, w.total, w.at) && !config_read(w, w.at)) {
            return nullptr;
        }
        if (!window_whole(buf, w.base, w.length, w.at)) {
            if (w.at - w.base + 2u > w.length) {
                return nullptr;        /* the device sent less than it said */
            }
            w.at = (uint16_t)(w.at + buf[w.at - w.base]);
            continue;
        }
        const uint8_t *d = buf + (w.at - w.base);
        return d[0] < 2 ? nullptr : d;
    }
}

const uint8_t *ch32rv_usb_host_config_first(CH32RVUsbHostConfigWalk &w, uint8_t daddr)
{
    w.daddr = daddr;
    w.base = w.length = w.at = 0;
    w.total = daddr && daddr <= CH32RV_USB_HOST_DEVICES ? devs[daddr].configLength : 0;
    if (w.total < 9) {
        return nullptr;
    }
    return config_here(w);
}

const uint8_t *ch32rv_usb_host_config_next(CH32RVUsbHostConfigWalk &w)
{
    if (w.at < w.base || w.at - w.base >= w.length) {
        return nullptr;
    }
    w.at = (uint16_t)(w.at + ch32rv_usb_host_scratch[w.at - w.base]);
    return config_here(w);
}

uint16_t ch32rv_usb_host_config_room(const CH32RVUsbHostConfigWalk &w)
{
    return w.at >= w.base && w.at - w.base < w.length ? (uint16_t)(w.length - (w.at - w.base)) : 0;
}

namespace {

/* ------------------------------------------------------------- enumeration
 * One device at a time, at address 0, as a state machine stepped from
 * pump(): it never blocks, so the classes' transfers go on meanwhile. */
enum : uint8_t {
    E_IDLE, E_DEBOUNCE, E_ROOT_RESET, E_HUB_RESET, E_HUB_POLL_WAIT, E_HUB_POLL, E_HUB_CLEAR, E_RECOVER,
    E_DEV8, E_ADDRESS, E_ADDRESS_WAIT, E_DEV18, E_BOS5, E_BOS, E_ISOCH_DELAY, E_CFG9, E_CFG, E_SET_CFG, E_LPM, E_MOUNT,
    E_CLAIM, E_CLAIM_READ,
};

/* SuperSpeed link power management after SET_CONFIGURATION (USB 3.2
 * 9.4.12, 9.4.9, 10.16.2.10): SET_SEL, U1_ENABLE, U2_ENABLE, and behind a
 * hub the port's U1 / U2 timeout. 1 sends them, 0 leaves every link in U0
 * (what the EVT does). */
#ifndef CH32RV_USB_HOST_SS_LINK_PM
#define CH32RV_USB_HOST_SS_LINK_PM 1
#endif

struct Request {
    uint8_t hub, port;             /* hub 0: the root port */
};
#define REQUESTS 4

struct Enumeration {
    uint8_t state;
    Request now;
    Request queue[REQUESTS];
    uint8_t queued;
    uint8_t speed, address, tries, polls;
    /* The window of the configuration in `buf` (from winBase, winLength
     * bytes) and the next descriptor the claim looks at; `moved`: the window
     * was just read for it. */
    uint16_t winBase, winLength, claimAt;
    bool moved;
    uint32_t t;
    bool again;                    /* the root port's device gave up on: try it again at `againAt` */
    uint32_t againAt;
    volatile bool waiting;         /* a control transfer of ours in flight */
    volatile uint8_t result;
    volatile uint16_t actual;
    uint8_t status[4] __attribute__((aligned(4)));
} en;   /* its window: en_buf */

void en_done(void *user, uint8_t result, uint16_t actual)
{
    (void)user;
    en.result = result;
    en.actual = actual;
    en.waiting = false;
}

/* A control transfer for the enumeration; false (try again later) while the
 * engine serves someone else. */
bool en_control(uint8_t daddr, uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                uint8_t *data, uint16_t length, uint16_t skip = 0)
{
    if (!control_start(daddr, requestType, request, value, index, data, length, en_done, nullptr, skip)) {
        return false;
    }
    en.waiting = true;
    return true;
}

void en_queue(uint8_t hub, uint8_t port)
{
    for (uint8_t i = 0; i < en.queued; i++) {
        if (en.queue[i].hub == hub && en.queue[i].port == port) {
            return;
        }
    }
    if ((en.state != E_IDLE && en.now.hub == hub && en.now.port == port) || en.queued >= REQUESTS) {
        return;
    }
    en.queue[en.queued++] = {hub, port};
}

/* Drop requests for a port (it went away), or every request (hub 0xFF). */
void en_unqueue(uint8_t hub, uint8_t port)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < en.queued; i++) {
        if (!(hub == 0xFF || (en.queue[i].hub == hub && en.queue[i].port == port))) {
            en.queue[n++] = en.queue[i];
        }
    }
    en.queued = n;
    if (en.state != E_IDLE && (hub == 0xFF || (en.now.hub == hub && en.now.port == port))) {
        if (ctl.stage != CTL_IDLE && ctl.done == en_done) {
            control_abort();
        }
        en.waiting = false;
        hcd->deviceClose(0);
        const uint8_t a = en.address;
        en.address = 0;
        en.state = E_IDLE;
        if (a) {
            remove_device(a);          /* the groups that took interfaces in the claim let go too */
        }
    }
}

uint8_t free_address(void)
{
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
        if (devs[a].state == DEV_FREE) {
            return a;
        }
    }
    return 0;
}

/* Something went wrong: reset the port and start over, three times. */
void en_retry(void)
{
    hcd->deviceClose(0);
    if (en.address) {
        hcd->deviceClose(en.address);
        memset(&devs[en.address], 0, sizeof devs[en.address]);
        en.address = 0;
    }
    if (++en.tries < 3) {
        en.state = E_DEBOUNCE;
        en.t = millis();
    } else {
        en.state = E_IDLE;
        /* On the root port, three times without an answer is not the end: a device still starting, or one
         * a debugger holds, answers later, and a quick restart of it need not show as a detach (it came
         * back before DETECT saw it go). While it stays attached, it is tried again every 2 s. Behind a
         * hub, the hub's port change brings it back. */
        if (en.now.hub == 0) {
            en.again = true;
            en.againAt = millis() + 2000;
        }
    }
}

/* The claim is over: the device is the sketch's. */
void en_mounted(void)
{
    Device &d = devs[en.address];
    d.info.supported = d.claimed != 0;
    d.state = DEV_MOUNTED;
    d.configurePending = true;
    d.infoPending = true;
    en.address = 0;
    en.state = E_IDLE;
}

/* Offer the interfaces of the new configuration to the groups, a window of
 * it at a time: an interface is offered with the window starting at (or
 * just before) it whenever the configuration goes on past the window, so its
 * class sees as much of what follows as a window holds. */
void en_claim(void)
{
    Device &d = devs[en.address];
    const uint16_t total = d.configLength;
    for (;;) {
        const uint16_t at = en.claimAt;
        if (at + 2u > total) {
            en_mounted();
            return;
        }
        const uint16_t mps = d.mps0 ? d.mps0 : 8;
        const bool more = en.winBase + (uint32_t)en.winLength < total;
        bool read = !window_holds(en_buf, en.winBase, en.winLength, total, at);
        if (!read && more && at - en.winBase >= mps) {
            const uint8_t *p = en_buf + (at - en.winBase);
            read = p[0] >= 9 && p[1] == 4;   /* an interface: give its class a window from here */
        }
        if (read && !en.moved) {
            uint16_t base, length;
            window_for(en.address, total, at, &base, &length);
            if (en_control(en.address, 0x80, 6, 0x0200, 0, en_buf, length, base)) {
                en.winBase = base;
                en.state = E_CLAIM_READ;
            }
            return;                    /* sent: wait for it; not: the engine is busy, again later */
        }
        en.moved = false;
        if (!window_whole(en_buf, en.winBase, en.winLength, at)) {
            if (at - en.winBase + 2u > en.winLength) {
                en_mounted();          /* the device sent less than it said */
                return;
            }
            en.claimAt = (uint16_t)(at + en_buf[at - en.winBase]);   /* too long for a window: stepped over */
            continue;
        }
        const uint8_t *p = en_buf + (at - en.winBase);
        if (p[0] < 2) {
            en_mounted();
            return;
        }
        const uint16_t room = (uint16_t)(en.winLength - (at - en.winBase));
        if (p[1] != 4 || p[0] < 9 || p[3] != 0 || p[2] >= 16 || (d.claimed & (1u << p[2]))) {
            en.claimAt = (uint16_t)(at + p[0]);
            continue;
        }
        uint16_t used = 0;
        for (CH32RVUsbHostGroup *g = groups; g && !used; g = g->next) {
            if (g->open && (used = g->open(en.address, p, room)) != 0) {
                /* Every interface the group's span covers is its. */
                for (uint16_t k = 0; k < used && k + 2u <= room; k = (uint16_t)(k + p[k])) {
                    if (p[k] < 2) {
                        break;
                    }
                    if (p[k + 1] == 4 && p[k + 2] < 16) {
                        d.owner[p[k + 2]] = g;
                        d.claimed = (uint16_t)(d.claimed | (1u << p[k + 2]));
                    }
                }
            }
        }
        en.claimAt = (uint16_t)(at + (used ? used : p[0]));
    }
}

#if CH32RV_USB_HOST_SUPERSPEED
/* The enumeration's SuperSpeed steps (BOS, SET_ISOCH_DELAY, link power
 * management): apart, so that they go when no SuperSpeed host is linked. */
void en_super_step(void)
{
    switch (en.state) {
    case E_BOS5: {                     /* SuperSpeed: the BOS (required, USB 3.2 9.6.2) */
        uint16_t total = (uint16_t)(en_buf[2] | en_buf[3] << 8);
        if (en.result != CH32RV_USBH_OK || en.actual < 5 || total < 5) {
            total = 0;                 /* go on without the latencies */
        }
        if (total > CH32RV_USB_HOST_SCRATCH) {
            total = CH32RV_USB_HOST_SCRATCH;
        }
        if (!total) {
            en.actual = 0;
            en.result = CH32RV_USBH_OK;
            en.state = E_BOS;
        } else if (en_control(en.address, 0x80, 6, 0x0F00, 0, en_buf, total)) {
            en.state = E_BOS;
        }
        return;
    }
    case E_BOS: {
        Device &d = devs[en.address];
        if (en.result == CH32RV_USBH_OK) {
            /* The SuperSpeed USB Device Capability (type 3): U1 and U2 exit
             * latencies at 7 and 8-9. */
            for (uint16_t at = 5; at + 3 <= en.actual && en_buf[at] >= 3; at = (uint16_t)(at + en_buf[at])) {
                if (en_buf[at + 1] == 0x10 && en_buf[at + 2] == 3 && en_buf[at] >= 10 && at + 10 <= en.actual) {
                    SSD(d).u1Latency = en_buf[at + 7];
                    SSD(d).u2Latency = (uint16_t)(en_buf[at + 8] | en_buf[at + 9] << 8);
                }
            }
        }
        /* SET_ISOCH_DELAY (USB 3.2 9.4.11): 40 ns of the root port (a guess
         * for this host; the EVT sends 0xFF, enum.c:422) plus the hubs'
         * delays. Some devices stall it (the EVT's note, enum.c:920): the
         * result is not looked at. */
        if (en_control(en.address, 0x00, 0x31, (uint16_t)(40u + SSD(d).hubDelay), 0, nullptr, 0)) {
            en.state = E_ISOCH_DELAY;
        }
        return;
    }
    case E_ISOCH_DELAY:
        if (en_control(en.address, 0x80, 6, 0x0200, 0, en_buf, 9)) {
            en.state = E_CFG9;
        }
        return;
    case E_LPM: {                      /* one request per step; their results are not looked at */
        const Device &d = devs[en.address];
        while (SS.lpm < 5) {
            const uint8_t step = SS.lpm;
            if (step >= 3 && !d.parent) {
                SS.lpm++;
                continue;
            }
            bool sent;
            if (step == 0) {
                /* SET_SEL: U1SEL, U1PEL, U2SEL, U2PEL (us). PEL is the
                 * device's own exit latency; SEL adds 1 us per hub tier and
                 * 1 us for this host's link. Guess: the host's own exit
                 * latency is not documented; measure: U1 / U2 exits the
                 * device times out on. */
                const uint16_t u1 = SSD(d).u1Latency, u2 = SSD(d).u2Latency;
                const uint16_t extra = (uint16_t)(1u + SSD(d).tier);
                const uint16_t u1sel = (uint16_t)(u1 + extra), u2sel = (uint16_t)(u2 + extra);
                SS.sel[0] = (uint8_t)(u1sel > 0xFF ? 0xFF : u1sel);
                SS.sel[1] = (uint8_t)(u1 > 0xFF ? 0xFF : u1);
                SS.sel[2] = (uint8_t)u2sel;
                SS.sel[3] = (uint8_t)(u2sel >> 8);
                SS.sel[4] = (uint8_t)u2;
                SS.sel[5] = (uint8_t)(u2 >> 8);
                sent = en_control(en.address, 0x00, 0x30, 0, 0, SS.sel, 6);
            } else if (step == 1) {
                sent = en_control(en.address, 0x00, 3, 48, 0, nullptr, 0);   /* SET_FEATURE(U1_ENABLE) */
            } else if (step == 2) {
                sent = en_control(en.address, 0x00, 3, 49, 0, nullptr, 0);   /* SET_FEATURE(U2_ENABLE) */
            } else {
                /* The hub's port accepts the device's U1 / U2 but does not
                 * start them itself (timeout 0xFF, USB 3.2 10.16.2.10). */
                const uint16_t feature = step == 3 ? 23 : 24;   /* PORT_U1_TIMEOUT / PORT_U2_TIMEOUT */
                sent = en_control(d.parent, 0x23, 3, feature, (uint16_t)(d.port | 0xFF00u), nullptr, 0);
            }
            if (sent) {
                SS.lpm++;
            }
            return;                    /* sent: wait for it; not: the engine is busy, again later */
        }
        if (d.parent == 0 && hcd->linkAllow) {
            hcd->linkAllow(true, true);
        }
        en.state = E_MOUNT;
        return;
    }
    default:
        return;
    }
}
#endif

void en_step(void)
{
    if (en.waiting) {
        return;
    }
    const uint32_t now = millis();
#if CH32RV_USB_HOST_SUPERSPEED
    if (SS_ON && (en.state == E_BOS5 || en.state == E_BOS || en.state == E_ISOCH_DELAY || en.state == E_LPM)) {
        en_super_step();
        return;
    }
#endif
    switch (en.state) {
    case E_IDLE:
        if (!en.queued && en.again && (int32_t)(now - en.againAt) >= 0) {
            en.again = false;
            bool present = false;
            for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
                present = present || (devs[a].state != DEV_FREE && devs[a].parent == 0);
            }
            if (hcd->connected() && !present) {
                en.queue[en.queued++] = {0, 0};
            }
        }
        if (!en.queued) {
            return;
        }
        en.now = en.queue[0];
        memmove(en.queue, en.queue + 1, (size_t)(--en.queued) * sizeof en.queue[0]);
        en.tries = 0;
        en.address = 0;
        en.t = now;
        en.state = E_DEBOUNCE;
        return;
    case E_DEBOUNCE:                   /* USB 2.0 7.1.7.3: 100 ms after the attach */
        if ((uint32_t)(now - en.t) < 100) {
            return;
        }
        if (en.now.hub == 0) {
            if (!hcd->connected()) {
                en.state = E_IDLE;
                return;
            }
            hcd->portReset();
            en.t = now;
            en.state = E_ROOT_RESET;
#if CH32RV_USB_HOST_SUPERSPEED
        } else if (is_super(devs[en.now.hub].speed)) {
            /* A SuperSpeed hub's port trains by itself and is Enabled once
             * the link is in U0: no PORT_RESET (USB 3.2 10.3.1.2), wait for it. */
            en.polls = 0;
            SS.warmed = false;
            en.t = now;
            en.state = E_HUB_POLL_WAIT;
#endif
        } else if (en_control(en.now.hub, 0x23, 3, 4, en.now.port, nullptr, 0)) {   /* SET_FEATURE(PORT_RESET) */
            en.state = E_HUB_RESET;
        }
        return;
    case E_ROOT_RESET:                 /* at least 10 ms; 20 as most hosts */
        if ((uint32_t)(now - en.t) < 20) {
            return;
        }
        en.speed = hcd->portResetEnd();
        if (en.speed == CH32RV_USBH_RESET_PENDING) {
            return;                    /* a warm reset still running: ask again */
        }
        en.t = now;
        en.state = E_RECOVER;
        return;
    case E_HUB_RESET:
        if (en.result != CH32RV_USBH_OK) {
            en.state = E_IDLE;
            return;
        }
        en.polls = 0;
        en.t = now;
        en.state = E_HUB_POLL_WAIT;
        return;
    case E_HUB_POLL_WAIT:
        if ((uint32_t)(now - en.t) < 10) {
            return;
        }
        if (en_control(en.now.hub, 0xA3, 0, 0, en.now.port, en.status, 4)) {   /* GET_STATUS(port) */
            en.state = E_HUB_POLL;
        }
        return;
    case E_HUB_POLL: {
        const uint16_t status = (uint16_t)(en.status[0] | en.status[1] << 8);
        const uint16_t change = (uint16_t)(en.status[2] | en.status[3] << 8);
        if (en.result != CH32RV_USBH_OK || !(status & 1)) {
            en.state = E_IDLE;         /* gone, or the hub does not answer */
            return;
        }
#if CH32RV_USB_HOST_SUPERSPEED
        if (is_super(devs[en.now.hub].speed)) {
            /* SuperSpeed port status (USB 3.2 10.16.2.6): bit 1 enabled,
             * bits 8:5 the link state, 0 U0, 6 SS.Inactive - which only a
             * warm reset (BH_PORT_RESET) leaves. */
            const uint8_t link = (uint8_t)((status >> 5) & 0x0F);
            if ((status & 0x02) && link == 0) {
                en.speed = CH32RV_USBH_SUPER;
                en.t = now;
                en.state = E_RECOVER;
                return;
            }
            if (++en.polls > 50) {
                en.state = E_IDLE;
                return;
            }
            en.t = now;
            en.state = E_HUB_POLL_WAIT;
            if (link == 6 && !SS.warmed && en_control(en.now.hub, 0x23, 3, 28, en.now.port, nullptr, 0)) {
                SS.warmed = true;      /* SET_FEATURE(BH_PORT_RESET); E_HUB_POLL_WAIT ignores its result */
            }
            return;
        }
#endif
        if (!(change & 0x10) || (status & 0x10)) {   /* C_PORT_RESET not yet, or still resetting */
            if (++en.polls > 50) {
                en.state = E_IDLE;
                return;
            }
            en.t = now;
            en.state = E_HUB_POLL_WAIT;
            return;
        }
        en.speed = (status & 0x0400) ? CH32RV_USBH_HIGH : (status & 0x0200) ? CH32RV_USBH_LOW : CH32RV_USBH_FULL;
        if (en_control(en.now.hub, 0x23, 1, 20, en.now.port, nullptr, 0)) {   /* CLEAR_FEATURE(C_PORT_RESET) */
            en.state = E_HUB_CLEAR;
        }
        return;
    }
    case E_HUB_CLEAR:
        en.t = now;
        en.state = E_RECOVER;
        return;
    case E_RECOVER:                    /* 10 ms of reset recovery (7.1.7.5) */
        if ((uint32_t)(now - en.t) < 10) {
            return;
        }
        devs[0].speed = en.speed;
        devs[0].state = DEV_ENUM;
#if CH32RV_USB_HOST_SUPERSPEED
        /* Where it sits: on the root port the route is 0; behind a hub of
         * tier t its port goes in bits 4t+3:4t (USB 3.2 8.9), and each hub
         * adds its delay (wHubDelay) to the isochronous delay. */
        if (SS_ON) {
            CH32RVUsbHostSsDevice &n = SS.now;
            n = CH32RVUsbHostSsDevice();
            if (en.now.hub) {
                const CH32RVUsbHostSsDevice &h = SSD(devs[en.now.hub]);
                n.route = h.tier < 5 ? h.route | ((uint32_t)(en.now.port > 15 ? 15 : en.now.port) << (4u * h.tier)) : h.route;
                n.tier = (uint8_t)(h.tier + 1u);
                n.hubDelay = h.hubDelay;
            }
            if (hcd->deviceAt) {
                hcd->deviceAt(0, n.route);
            }
        }
#endif
        /* SuperSpeed EP0 is 512 (bMaxPacketSize0 9); USB 2.0 starts at 8. */
        if (!ep0_open(0, is_super(en.speed) ? 512 : 8, en.speed, en.now.hub, en.now.port)) {
            en_retry();
            return;
        }
        if (en_control(0, 0x80, 6, 0x0100, 0, en_buf, 8)) {   /* the first 8 bytes of the device descriptor */
            en.state = E_DEV8;
        }
        return;
    case E_DEV8: {
        if (en.result != CH32RV_USBH_OK || en.actual < 8) {
            en_retry();
            return;
        }
        const uint8_t a = free_address();
        if (!a) {
            hcd->deviceClose(0);
            en.state = E_IDLE;         /* no room for another device */
            return;
        }
        Device &d = devs[a];
        memset(&d, 0, sizeof d);
        d.state = DEV_ENUM;
        d.parent = en.now.hub;
        d.port = en.now.port;
        d.speed = en.speed;
#if CH32RV_USB_HOST_SUPERSPEED
        if (SS_ON) {
            SSD(d) = SS.now;
        }
#endif
        /* USB 3.x (bcdUSB 0x0300 on): bMaxPacketSize0 is an exponent, 9 for
         * 512 at SuperSpeed (USB 3.2 9.6.1). */
        if ((uint16_t)(en_buf[2] | en_buf[3] << 8) >= 0x0300 && en_buf[7] >= 3 && en_buf[7] <= 9) {
            d.mps0 = (uint16_t)(1u << en_buf[7]);
        } else {
            d.mps0 = en_buf[7] >= 8 && en_buf[7] <= 64 ? en_buf[7] : 8;
        }
        en.address = a;
        if (en_control(0, 0x00, 5, a, 0, nullptr, 0)) {   /* SET_ADDRESS */
            en.state = E_ADDRESS;
        }
        return;
    }
    case E_ADDRESS:
        if (en.result != CH32RV_USBH_OK) {
            en_retry();
            return;
        }
        hcd->deviceClose(0);
        en.t = now;
        en.state = E_ADDRESS_WAIT;
        return;
    case E_ADDRESS_WAIT:               /* 2 ms to take the address (9.2.6.3) */
        if ((uint32_t)(now - en.t) < 2) {
            return;
        }
#if CH32RV_USB_HOST_SUPERSPEED
        if (SS_ON && hcd->deviceAt) {
            hcd->deviceAt(en.address, SS.now.route);
        }
#endif
        if (!ep0_open(en.address, devs[en.address].mps0, en.speed, en.now.hub, en.now.port)) {
            en_retry();
            return;
        }
        if (en_control(en.address, 0x80, 6, 0x0100, 0, devs[en.address].desc, 18)) {
            en.state = E_DEV18;
        }
        return;
    case E_DEV18:
        if (en.result != CH32RV_USBH_OK || en.actual < 18) {
            en_retry();
            return;
        }
#if CH32RV_USB_HOST_SUPERSPEED
        if (is_super(en.speed)) {
            if (en_control(en.address, 0x80, 6, 0x0F00, 0, en_buf, 5)) {   /* the BOS header */
                en.state = E_BOS5;
            }
            return;
        }
#endif
        if (en_control(en.address, 0x80, 6, 0x0200, 0, en_buf, 9)) {
            en.state = E_CFG9;
        }
        return;
    case E_CFG9: {
        if (en.result != CH32RV_USBH_OK || en.actual < 9) {
            en_retry();
            return;
        }
        uint16_t total = (uint16_t)(en_buf[2] | en_buf[3] << 8);
        Device &d = devs[en.address];
        d.configLength = total;
        d.configInterfaces = en_buf[4];
        d.configValue = en_buf[5];
        d.configAttributes = en_buf[7];
        d.configPower = en_buf[8];
        if (total < 9) {
            en_retry();
            return;
        }
        /* The first window; the claim reads further ones as it goes. */
        window_for(en.address, total, 0, &en.winBase, &en.winLength);
        if (en_control(en.address, 0x80, 6, 0x0200, 0, en_buf, en.winLength)) {
            en.state = E_CFG;
        }
        return;
    }
    case E_CFG:
        if (en.result != CH32RV_USBH_OK || en.actual < 9) {
            en_retry();
            return;
        }
        en.winLength = en.actual;
        if (en_control(en.address, 0x00, 9, devs[en.address].configValue, 0, nullptr, 0)) {   /* SET_CONFIGURATION */
            en.state = E_SET_CFG;
        }
        return;
    case E_SET_CFG:
        if (en.result != CH32RV_USBH_OK) {
            en_retry();
            return;
        }
#if CH32RV_USB_HOST_SUPERSPEED
        if (is_super(en.speed) && CH32RV_USB_HOST_SS_LINK_PM) {
            SS.lpm = 0;
            en.state = E_LPM;
            return;
        }
#endif
        en.state = E_MOUNT;
        return;
    case E_MOUNT:
        en.claimAt = en_buf[0] >= 9 ? en_buf[0] : 9;
        en.moved = false;
        en.state = E_CLAIM;
        en_claim();
        return;
    case E_CLAIM:
        en_claim();
        return;
    case E_CLAIM_READ:
        if (en.result != CH32RV_USBH_OK || !en.actual) {
            en_mounted();              /* what is claimed so far stays */
            return;
        }
        en.winLength = en.actual;
        en.moved = true;
        en.state = E_CLAIM;
        en_claim();
        return;
    }
}

/* ------------------------------------------------------------- hubs
 * Built in: a device behind a hub is ordinary. */
struct Hub {
    uint8_t daddr, ep, ports;
    volatile bool armed;
    volatile uint16_t changed;     /* bit n: port n (bit 0 the hub itself) */
    uint8_t bitmap[2] __attribute__((aligned(4)));
};
Hub hubs[2];

Hub *hub_of(uint8_t daddr)
{
    for (Hub &h : hubs) {
        if (h.daddr == daddr && daddr) {
            return &h;
        }
    }
    return nullptr;
}

uint16_t hub_open(uint8_t daddr, const uint8_t *itf, uint16_t length)
{
    if (itf[5] != 9 || length < 9 + 7) {
        return 0;
    }
    const uint8_t *ep = ch32rv_usb_desc_next(itf);
    if (ep[1] != 5 || !(ep[2] & 0x80) || (ep[3] & 3) != 3) {
        return 0;
    }
    for (Hub &h : hubs) {
        if (!h.daddr) {
            if (!ch32rv_usb_host_ep_open(daddr, ep)) {
                return 0;
            }
            memset(&h, 0, sizeof h);
            h.daddr = daddr;
            h.ep = ep[2];
            uint16_t used = (uint16_t)(itf[0] + ep[0]);
            if (used + 2 <= length && itf[used] >= 6 && itf[used + 1] == 0x30) {
                used = (uint16_t)(used + itf[used]);   /* SuperSpeed: the endpoint companion */
            }
            return used;
        }
    }
    return 0;
}

void hub_status_done(void *user, uint8_t result, uint16_t length)
{
    Hub &h = *(Hub *)user;
    h.armed = false;
    if (result == CH32RV_USBH_OK && length) {
        h.changed = (uint16_t)(h.changed | h.bitmap[0] | (length > 1 ? h.bitmap[1] << 8 : 0));
    }
}

void hub_arm(Hub &h)
{
    if (h.daddr && !h.armed) {
        h.armed = ch32rv_usb_host_xfer(h.daddr, h.ep, h.bitmap, sizeof h.bitmap, hub_status_done, &h);
    }
}

/* Power the ports, then listen for changes. A SuperSpeed hub (USB 3.2
 * 10.15.2): its descriptor is type 0x2A, 12 bytes with wHubDelay at 10, and
 * it is told its tier (SET_HUB_DEPTH) before its ports are looked at. */
void hub_configured(uint8_t daddr, uint8_t itf)
{
    (void)itf;
    Hub *h = hub_of(daddr);
    if (!h) {
        return;
    }
    Device &d = devs[daddr];
    const bool super = is_super(d.speed);
    uint8_t desc[12] = {};
    uint16_t got = 0;
    if (ch32rv_usb_host_control_sync(daddr, 0xA0, 6, super ? 0x2A00 : 0x2900, 0, desc, super ? 12 : 9, &got, 1000) !=
            CH32RV_USBH_OK ||
        got < 7) {
        return;
    }
#if CH32RV_USB_HOST_SUPERSPEED
    if (super) {
        if (got >= 12) {
            SSD(d).hubDelay = (uint16_t)(SSD(d).hubDelay + (desc[10] | desc[11] << 8));
        }
        ch32rv_usb_host_control_sync(daddr, 0x20, 12, SSD(d).tier, 0, nullptr, 0, nullptr, 1000);   /* SET_HUB_DEPTH */
    }
#endif
    h->ports = desc[2] > 15 ? 15 : desc[2];
    for (uint8_t p = 1; p <= h->ports; p++) {
        ch32rv_usb_host_control_sync(daddr, 0x23, 3, 8, p, nullptr, 0, nullptr, 1000);   /* SET_FEATURE(PORT_POWER) */
    }
    const uint32_t t0 = millis();
    const uint32_t settle = (uint32_t)desc[5] * 2u + 20u;   /* bPwrOn2PwrGood, in 2 ms */
    while ((uint32_t)(millis() - t0) < settle) {
        pump();
    }
    h->changed = (uint16_t)(((1u << (h->ports + 1)) - 1u) & ~1u);   /* look at every port once */
    hub_arm(*h);
}

void hub_poll(CH32RVUsbHost &host)
{
    (void)host;
    for (Hub &h : hubs) {
        if (!h.daddr || !ch32rv_usb_host_connected(h.daddr)) {
            continue;
        }
        const uint16_t changed = h.changed;
        h.changed = 0;
        for (uint8_t p = 1; p <= h.ports; p++) {
            if (!(changed & (1u << p))) {
                continue;
            }
            uint8_t st[4] = {};
            if (ch32rv_usb_host_control_sync(h.daddr, 0xA3, 0, 0, p, st, 4, nullptr, 1000) != CH32RV_USBH_OK) {
                continue;
            }
            const uint16_t status = (uint16_t)(st[0] | st[1] << 8);
            const uint16_t change = (uint16_t)(st[2] | st[3] << 8);
            /* The change bits and their CLEAR_FEATURE selectors: USB 2.0
             * 11.24.2.7.2 (C_PORT_* = 16 + bit), a SuperSpeed hub's USB 3.2
             * 10.16.2.6 (connection 16, over-current 19, reset 20, warm
             * reset 29, link state 25, config error 26). */
            static const uint8_t hs_clear[] = {16, 17, 18, 19, 20, 0, 0, 0};
            static const uint8_t ss_clear[] = {16, 0, 0, 19, 20, 29, 25, 26};
            const uint8_t *clear = is_super(devs[h.daddr].speed) ? ss_clear : hs_clear;
            for (uint8_t b = 0; b < 8; b++) {
                if ((change & (1u << b)) && clear[b]) {
                    ch32rv_usb_host_control_sync(h.daddr, 0x23, 1, clear[b], p, nullptr, 0, nullptr, 1000);
                }
            }
            if (!(change & 1) && changed != (uint16_t)(((1u << (h.ports + 1)) - 1u) & ~1u)) {
                continue;              /* no connection change */
            }
            bool present = false;
            for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
                if (devs[a].state != DEV_FREE && devs[a].parent == h.daddr && devs[a].port == p) {
                    present = true;
                    if (!(status & 1) || (change & 1)) {
                        remove_device(a);
                        present = false;
                    }
                }
            }
            if (status & 1) {
                if (!present) {
                    en_queue(h.daddr, p);
                }
            } else {
                en_unqueue(h.daddr, p);
            }
        }
        hub_arm(h);
    }
}

void hub_detach(uint8_t daddr)
{
    if (Hub *h = hub_of(daddr)) {
        memset(h, 0, sizeof *h);
    }
}

CH32RVUsbHostGroup hub_group = {hub_open, hub_configured, hub_poll, hub_detach, nullptr, nullptr};

struct HubJoin {
    HubJoin() { ch32rv_usb_host_join(hub_group); }
} hub_join;

/* ------------------------------------------------------------- devices */
void remove_device(uint8_t daddr)
{
    Device &d = devs[daddr];
    if (d.state == DEV_FREE) {
        return;
    }
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
        if (a != daddr && devs[a].state != DEV_FREE && devs[a].parent == daddr) {
            remove_device(a);          /* behind it, if it is a hub */
        }
    }
    en_unqueue(daddr, 0);
    for (uint8_t p = 1; p < 16; p++) {
        en_unqueue(daddr, p);
    }
    if (ctl.stage != CTL_IDLE && ctl.daddr == daddr) {
        control_abort();
        control_finish(CH32RV_USBH_FAILED);
    }
    const bool was_known = d.known;
    d.state = DEV_FREE;
    d.known = false;
    for (CH32RVUsbHostGroup *g = groups; g; g = g->next) {
        if (g->detach) {
            g->detach(daddr);
        }
    }
    hcd->deviceClose(daddr);
    pending_drop(daddr, 0xFF);
    events_void(daddr, 0xFF);
    CH32RVUsbHost *host = ch32rv_usb_host_active;
    const CH32RVUsbHostDeviceInfo info = d.info;
    /* Its strings are freed: a connection of it not yet delivered loses them. */
    for (uint8_t i = 0; i < device_count; i++) {
        DeviceEvent &e = device_events[(uint8_t)(device_first + i) % DEVICE_EVENTS];
        if (e.connected && e.info.address == daddr) {
            e.info.manufacturer = e.info.product = e.info.serial = "";
        }
    }
    memset(&d, 0, sizeof d);
    if (was_known && host && host->_disconnected) {
        device_event(false, info);
    }
}

void root_changed(void)
{
    en.again = false;              /* plugged or unplugged: the usual way from here */
    if (hcd->connected()) {
        bool present = false;
        for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
            present = present || (devs[a].state != DEV_FREE && devs[a].parent == 0);
        }
        if (!present) {
            en_queue(0, 0);
        }
        return;
    }
    en_unqueue(0, 0);
    root_suspended = false;
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
        if (devs[a].state != DEV_FREE && devs[a].parent == 0) {
            remove_device(a);
        }
    }
}

/* What the stack does whenever it gets the chance: the root port, the
 * completions, the next enumeration step. Never the groups' poll or
 * configured() - those run only from the task itself. */
void pump(void)
{
    if (!hcd) {
        return;
    }
    if (port_changed) {
        port_changed = false;
        root_changed();
    }
    if (power_pump) {
        power_pump();
    }
    while (ev_tail != ev_head) {
        const uint8_t i = ev_tail;
        const uint8_t daddr = events[i].daddr, address = events[i].address, result = events[i].result;
        const uint16_t length = events[i].length;
        ev_tail = (uint8_t)((i + 1) % EVENTS);
        dispatch(daddr, address, result, length);
    }
    en_step();
}

/* UTF-16LE string descriptor -> ASCII. `out` may be `desc` itself: each
 * character is written before the bytes it came from. */
void ascii_from_utf16(const uint8_t *desc, uint16_t got, char *out, size_t capacity)
{
    size_t n = 0;
    if (got >= 2 && desc[0] >= 2 && desc[1] == 3) {
        const size_t length = desc[0] < got ? desc[0] : got;
        for (size_t i = 2; i + 1 < length && n + 1 < capacity; i += 2) {
            const uint16_t c = (uint16_t)(desc[i] | (desc[i + 1] << 8));
            out[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        }
    }
    out[n] = '\0';
}

/* String `index` of the device (k: 0 manufacturer, 1 product, 2 serial)
 * into the pool, whole. */
void read_string(uint8_t daddr, uint8_t k, uint8_t index)
{
    Device &d = devs[daddr];
    d.stringBytes[k] = 0;
    if (!index) {
        return;
    }
    /* Read into the scratch buffer and made ASCII in place (each character
     * lands before the two bytes it came from). */
    uint8_t *buf = ch32rv_usb_host_scratch;
    uint16_t got = 0;
    if (ch32rv_usb_host_control_sync(daddr, 0x80, 6, (uint16_t)(0x0300 | index), 0x0409, buf, 255, &got, 1000) !=
            CH32RV_USBH_OK ||
        d.state != DEV_MOUNTED) {
        return;                    /* failed, or gone while we asked */
    }
    char *text = (char *)buf;
    ascii_from_utf16(buf, got, text, 255);
    uint16_t bytes = (uint16_t)(strlen(text) + 1), at = 0;
    if (bytes < 2 || !string_alloc(&at, &bytes)) {
        return;
    }
    memcpy(string_pool + at, text, bytes - 1u);
    string_pool[at + bytes - 1u] = '\0';
    d.stringAt[k] = at;
    d.stringBytes[k] = (uint8_t)bytes;
}

/* Fill in a newly mounted device and tell the sketch. From the task. */
void complete_mount(CH32RVUsbHost &host, uint8_t daddr)
{
    Device &s = devs[daddr];
    s.infoPending = false;
    CH32RVUsbHostDeviceInfo &d = s.info;
    const bool supported = s.claimed != 0;
    d = CH32RVUsbHostDeviceInfo();
    d.address = daddr;
    d.supported = supported;
    const uint8_t *dev = s.desc;
    d.vid = (uint16_t)(dev[8] | dev[9] << 8);
    d.pid = (uint16_t)(dev[10] | dev[11] << 8);
    d.usbVersion = (uint16_t)(dev[2] | dev[3] << 8);
    d.deviceVersion = (uint16_t)(dev[12] | dev[13] << 8);
    d.deviceClass = dev[4];
    d.deviceSubClass = dev[5];
    d.deviceProtocol = dev[6];
    d.maxPacketSize0 = dev[7];
    d.isHub = dev[4] == 9;
    d.parentAddress = s.parent;
    d.portId = s.parent ? (uint8_t)((s.parent << 4) | (s.port & 0x0F)) : 0x01;
    d.speed = s.speed == CH32RV_USBH_LOW     ? CH32RV_USB_HOST_SPEED_LOW
              : s.speed == CH32RV_USBH_HIGH  ? CH32RV_USB_HOST_SPEED_HIGH
              : is_super(s.speed)            ? CH32RV_USB_HOST_SPEED_SUPER
                                             : CH32RV_USB_HOST_SPEED_FULL;
    d.configurationValue = s.configValue;
    d.configurationAttributes = s.configAttributes;
    d.configurationMaxPower = s.configPower;
    d.configurationInterfaceCount = s.configInterfaces;
    d.configurationTotalLength = s.configLength;
    for (uint8_t k = 0; k < 3 && s.state == DEV_MOUNTED; k++) {
        read_string(daddr, k, dev[14 + k]);
    }
    if (s.state != DEV_MOUNTED) {
        return;                    /* gone while we asked */
    }
    d.manufacturer = string_of(s, 0);
    d.product = string_of(s, 1);
    d.serial = string_of(s, 2);
    s.known = true;
    if (host._connected) {
        device_event(true, d);
    }
}

}  // namespace

void ch32rv_usb_host_reenumerate(uint8_t daddr)
{
    if (!hcd || !daddr || daddr > CH32RV_USB_HOST_DEVICES) {
        return;
    }
    const uint8_t parent = devs[daddr].parent, port = devs[daddr].port;
    remove_device(daddr);
    if (parent == 0) {
        hcd->portDisable();
    }
    en_queue(parent, port);
}

/* The device on the root port (a hub, or the device itself): 0 when none is mounted. */
static uint8_t root_device(void)
{
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
        if (devs[a].state == DEV_MOUNTED && devs[a].parent == 0) {
            return a;
        }
    }
    return 0;
}

/* ------------------------------------------------------------- the task */
extern "C" void ch32rv_usb_host_task(void)
{
    static volatile bool busy;
    CH32RVUsbHost *host = ch32rv_usb_host_active;
    if (!host || !host->_started || busy) {
        return;
    }
    busy = true;
    if (hcd->poll) {
        hcd->poll();
    }
    pump();
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
        Device &d = devs[a];
        if (d.state == DEV_MOUNTED && d.configurePending) {
            d.configurePending = false;
            for (uint8_t i = 0; i < 16 && d.state == DEV_MOUNTED; i++) {
                if (d.owner[i] && d.owner[i]->configured) {
                    d.owner[i]->configured(a, i);
                }
            }
        }
        if (d.state == DEV_MOUNTED && d.infoPending) {
            complete_mount(*host, a);
        }
    }
    for (CH32RVUsbHostGroup *g = groups; g; g = g->next) {
        if (g->poll) {
            g->poll(*host);
        }
    }
    busy = false;
}

#endif /* CH32RV_USB_HOST_STACK */

/* ------------------------------------------------------------- the class */
CH32RVUsbHost::CH32RVUsbHost() {}

const char *CH32RVUsbHost::lastErrorName() const
{
    switch (_error) {
    case CH32RVUsbHostError::None: return "None";
    case CH32RVUsbHostError::Unsupported: return "Unsupported";
    case CH32RVUsbHostError::Busy: return "Busy";
    case CH32RVUsbHostError::NotStarted: return "NotStarted";
    case CH32RVUsbHostError::NotConnected: return "NotConnected";
    case CH32RVUsbHostError::NotSupported: return "NotSupported";
    case CH32RVUsbHostError::InvalidArgument: return "InvalidArgument";
    case CH32RVUsbHostError::Timeout: return "Timeout";
    case CH32RVUsbHostError::Stalled: return "Stalled";
    case CH32RVUsbHostError::TransferFailed: return "TransferFailed";
    case CH32RVUsbHostError::NoMemory: return "NoMemory";
    case CH32RVUsbHostError::InvalidResponse: return "InvalidResponse";
    }
    return "?";
}

#if CH32RV_USB_HOST_STACK

/* The stack's system service (ch32rv_service.h): begin() attaches it, end()
 * takes it out of the round. */
static ch32rv_service_t host_service = CH32RV_SERVICE_INIT(ch32rv_usb_host_task);

/* The sketch's callbacks (ch32rv_deliver.h). Attached from the first begin()
 * on and left attached, so the disconnections end() reports still arrive. */
static CH32RVUsbHost *delivering;

static size_t host_deliver(void)
{
    CH32RVUsbHost *const host = delivering;
    size_t ran = 0;
    for (uint8_t n = device_count; n && device_count; n--) {
        const DeviceEvent e = device_events[device_first];
        device_first = (uint8_t)((device_first + 1) % DEVICE_EVENTS);
        device_count--;
        const CH32RVUsbHost::DeviceCallback cb = e.connected ? host->_connected : host->_disconnected;
        if (cb) {
            cb(e.info);
            ran++;
        }
    }
    if (!host->ready()) {
        return ran;
    }
    if (power_deliver) {
        ran += power_deliver(*host);
    }
    for (CH32RVUsbHostGroup *g = groups; g; g = g->next) {
        if (g->deliver) {
            ran += g->deliver(*host);
        }
    }
    return ran;
}

static ch32rv_delivery_t host_delivery = CH32RV_DELIVERY_INIT(host_deliver);

/* The windows without SuperSpeed; the USBSS driver's definition takes this
 * one's place when it is linked (CH32RVUsbHostInternal.h). */
#if CH32RV_USB_HOST_SUPERSPEED
__attribute__((weak))
#endif
extern const CH32RVUsbHostWindows ch32rv_usb_host_windows = {scratch_window, enumeration_window, 256, nullptr};

uint32_t CH32RVUsbHost::droppedDeviceEvents() const
{
    return device_dropped;
}

bool CH32RVUsbHost::beginOn(const CH32RVUsbHcd *driver)
{
    if (_started) {
        return ok();
    }
    memset(devs, 0, sizeof devs);
    memset(pending, 0, sizeof pending);
    device_first = device_count = 0;
    device_dropped = 0;
    memset(&ctl, 0, sizeof ctl);
    memset(&en, 0, sizeof en);
    memset(hubs, 0, sizeof hubs);
    ev_head = ev_tail = 0;
    port_changed = false;
    root_suspended = false;
    power = nullptr;
    if (!driver) {
        return fail(CH32RVUsbHostError::Unsupported);
    }
    if (driver->prepare && !driver->prepare()) {
        return fail(CH32RVUsbHostError::Unsupported);
    }
    if (!driver->open()) {
        if (driver->release) {
            driver->release();
        }
        return fail(CH32RVUsbHostError::Busy);
    }
    hcd = driver;
    ch32rv_usb_host_active = this;
    _started = true;
    /* delay() and the return of loop() run the stack from here on. */
    ch32rv_service_attach(&host_service);
    delivering = this;
    ch32rv_delivery_attach(&host_delivery);
    if (hcd->connected()) {
        en_queue(0, 0);
    }
    return ok();
}

void CH32RVUsbHost::end()
{
    if (!_started) {
        return;
    }
    en_unqueue(0xFF, 0);
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
        if (devs[a].state != DEV_FREE && devs[a].parent == 0) {
            remove_device(a);
        }
    }
    const CH32RVUsbHcd *driver = hcd;
    driver->close();
    hcd = nullptr;
    if (driver->release) {
        driver->release();
    }
    _started = false;
    ch32rv_service_detach(&host_service);
    if (ch32rv_usb_host_active == this) {
        ch32rv_usb_host_active = nullptr;
    }
}

bool CH32RVUsbHost::ready() const
{
    return _started;
}

/* The drivers' suspend, weakly: only the driver begin() linked resolves. */
extern const CH32RVUsbHcdPower ch32rv_usb_hcd_usbfs_power __attribute__((weak));
extern const CH32RVUsbHcdPower ch32rv_usb_hcd_usbhs1_power __attribute__((weak));
extern const CH32RVUsbHcdPower ch32rv_usb_hcd_usbhs2_power __attribute__((weak));
extern const CH32RVUsbHcdPower ch32rv_usb_hcd_usbhs2_b_power __attribute__((weak));
extern const CH32RVUsbHcdPower ch32rv_usb_hcd_usbss_power __attribute__((weak));

static const CH32RVUsbHcdPower *power_of(const CH32RVUsbHcd *driver)
{
    const CH32RVUsbHcdPower *const all[] = {&ch32rv_usb_hcd_usbfs_power, &ch32rv_usb_hcd_usbhs1_power,
                                            &ch32rv_usb_hcd_usbhs2_power, &ch32rv_usb_hcd_usbhs2_b_power,
                                            &ch32rv_usb_hcd_usbss_power};
    for (const CH32RVUsbHcdPower *p : all) {
        if (p && p->hcd == driver) {
            return p;
        }
    }
    return nullptr;
}

/* From the task: the device woke the bus, and the port resumes for it. */
static void wakeup_pump(void)
{
    if (!root_wakeup) {
        return;
    }
    root_wakeup = false;
    if (root_suspended && power) {
        root_suspended = false;
        power->portSuspend(false);
        if (wakeups_due < 0xFF) {
            wakeups_due++;
        }
    }
}

static size_t wakeup_deliver(CH32RVUsbHost &)
{
    size_t ran = 0;
    for (; wakeups_due; wakeups_due--) {
        if (remote_wakeup_cb) {
            remote_wakeup_cb();
            ran++;
        }
    }
    return ran;
}

void CH32RVUsbHost::onRemoteWakeup(RemoteWakeupCallback cb)
{
    remote_wakeup_cb = cb;
}

/* Every device that suspends with the port has its remote wakeup armed first
 * when its configuration offers it (SET_FEATURE(DEVICE_REMOTE_WAKEUP), USB 2.0
 * 9.4.9) - a hub's too, or a wakeup behind it stops there. Not on SuperSpeed,
 * where it is a function's (FUNCTION_SUSPEND), not the device's. */
bool CH32RVUsbHost::suspend()
{
    if (!_started) {
        return fail(CH32RVUsbHostError::NotStarted);
    }
    if (!power) {
        power = power_of(hcd);
        if (!power) {
            return fail(CH32RVUsbHostError::Unsupported);
        }
        root_wakeup = false;
        wakeups_due = 0;
        power_pump = wakeup_pump;
        power_deliver = wakeup_deliver;
    }
    if (root_suspended) {
        return ok();
    }
    if (!root_device()) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
        if (devs[a].state == DEV_MOUNTED && (devs[a].configAttributes & 0x20u) && !is_super(devs[a].speed) &&
            !ch32rv_usb_host_control(*this, a, 0x00, 3, 1, 0, nullptr, 0, nullptr, 1000)) {
            return false;
        }
    }
    if (!power->portSuspend(true)) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    root_suspended = true;
    return ok();
}

bool CH32RVUsbHost::resume()
{
    if (!_started) {
        return fail(CH32RVUsbHostError::NotStarted);
    }
    if (!root_suspended || !power) {
        return ok();
    }
    root_suspended = false;
    root_wakeup = false;
    if (!power->portSuspend(false)) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    return ok();
}

bool CH32RVUsbHost::suspended() const
{
    return root_suspended;
}

void CH32RVUsbHost::task()
{
    ch32rv_usb_host_task();
}

uint8_t CH32RVUsbHost::resolve(uint8_t address) const
{
    if (address != CH32RV_USB_HOST_ANY_ADDRESS) {
        return address && address <= CH32RV_USB_HOST_DEVICES && devs[address].known ? address : 0;
    }
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
        if (devs[a].known && !devs[a].info.isHub) {
            return a;
        }
    }
    return 0;
}

size_t CH32RVUsbHost::deviceCount() const
{
    size_t n = 0;
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
        n += devs[a].known ? 1u : 0u;
    }
    return n;
}

size_t CH32RVUsbHost::getDevices(CH32RVUsbHostDeviceInfo *devices, size_t maxDevices) const
{
    size_t n = 0;
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES && n < maxDevices; a++) {
        if (devs[a].known) {
            devices[n++] = devs[a].info;
        }
    }
    return n;
}

bool CH32RVUsbHost::getDevice(uint8_t address, CH32RVUsbHostDeviceInfo &device) const
{
    if (!address || address > CH32RV_USB_HOST_DEVICES || !devs[address].known) {
        return false;
    }
    device = devs[address].info;
    return true;
}

size_t CH32RVUsbHost::getInterfaces(uint8_t address, CH32RVUsbHostInterfaceInfo *interfaces, size_t maxInterfaces)
{
    const uint8_t daddr = resolve(address);
    if (!daddr) {
        fail(CH32RVUsbHostError::NotConnected);
        return 0;
    }
    CH32RVUsbHostConfigWalk w;
    size_t n = 0;
    for (const uint8_t *d = ch32rv_usb_host_config_first(w, daddr); d && n < maxInterfaces;
         d = ch32rv_usb_host_config_next(w)) {
        if (d[1] == 4 && d[0] >= 9) {
            CH32RVUsbHostInterfaceInfo &i = interfaces[n++];
            i = CH32RVUsbHostInterfaceInfo();
            i.number = d[2];
            i.alternate = d[3];
            i.endpointCount = d[4];
            i.interfaceClass = d[5];
            i.interfaceSubClass = d[6];
            i.interfaceProtocol = d[7];
            i.claimed = d[2] < 16 && (devs[daddr].claimed & (1u << d[2]));
        }
    }
    ok();
    return n;
}

size_t CH32RVUsbHost::getEndpoints(uint8_t address, CH32RVUsbHostEndpointInfo *endpoints, size_t maxEndpoints)
{
    const uint8_t daddr = resolve(address);
    if (!daddr) {
        fail(CH32RVUsbHostError::NotConnected);
        return 0;
    }
    CH32RVUsbHostConfigWalk w;
    size_t n = 0;
    uint8_t itf = 0;
    for (const uint8_t *d = ch32rv_usb_host_config_first(w, daddr); d && n < maxEndpoints;
         d = ch32rv_usb_host_config_next(w)) {
        if (d[1] == 4 && d[0] >= 9) {
            itf = d[2];
        } else if (d[1] == 5 && d[0] >= 7) {
            CH32RVUsbHostEndpointInfo &e = endpoints[n++];
            e.address = d[2];
            e.interfaceNumber = itf;
            e.attributes = d[3];
            e.maxPacketSize = (uint16_t)((d[4] | (d[5] << 8)) & 0x7FF);
            e.interval = d[6];
        }
    }
    ok();
    return n;
}

bool CH32RVUsbHost::getHubInfo(uint8_t hubAddress, CH32RVUsbHostHubInfo &hub)
{
    uint8_t desc[9] = {};
    size_t got = 0;
    /* A SuperSpeed hub's descriptor is type 0x2A; the fields read here sit
     * at the same places. */
    const bool super = hubAddress && hubAddress <= CH32RV_USB_HOST_DEVICES && is_super(devs[hubAddress].speed);
    if (!ch32rv_usb_host_control(*this, hubAddress, 0xA0, 6, super ? 0x2A00 : 0x2900, 0, desc, sizeof desc, &got,
                                 1000)) {
        return false;
    }
    if (got < 7) {
        return fail(CH32RVUsbHostError::InvalidResponse);
    }
    hub = CH32RVUsbHostHubInfo();
    hub.address = hubAddress;
    hub.portCount = desc[2];
    hub.characteristics = (uint16_t)(desc[3] | (desc[4] << 8));
    hub.powerOnToPowerGoodMs = (uint16_t)(desc[5] * 2u);
    hub.controllerCurrentMa = desc[6];
    return ok();
}

bool CH32RVUsbHost::getHubPortStatus(uint8_t hubAddress, uint8_t port, uint16_t &status, uint16_t &change)
{
    uint8_t r[4] = {};
    size_t got = 0;
    if (!ch32rv_usb_host_control(*this, hubAddress, 0xA3, 0, 0, port, r, sizeof r, &got, 1000)) {
        return false;
    }
    status = (uint16_t)(r[0] | (r[1] << 8));
    change = (uint16_t)(r[2] | (r[3] << 8));
    return ok();
}

bool CH32RVUsbHost::setHubPortPower(uint8_t hubAddress, uint8_t port, bool enable)
{
    return ch32rv_usb_host_control(*this, hubAddress, 0x23, enable ? 3 : 1, 8, port, nullptr, 0, nullptr, 1000);
}

bool CH32RVUsbHost::vendorControlTransfer(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                                          uint8_t *data, size_t length, size_t *actualLength, uint8_t address,
                                          uint32_t timeoutMs)
{
    if (length > 0xFFFFu || (length && !data)) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    const uint8_t daddr = resolve(address);
    if (!daddr) {
        return fail(_started ? CH32RVUsbHostError::NotConnected : CH32RVUsbHostError::NotStarted);
    }
    return ch32rv_usb_host_control(*this, daddr, requestType, request, value, index, data, (uint16_t)length,
                                   actualLength, timeoutMs);
}

bool CH32RVUsbHost::vendorControlIn(uint8_t request, uint16_t value, uint16_t index, uint8_t *data,
                                    size_t length, size_t *actualLength, uint8_t address, uint32_t timeoutMs)
{
    /* Device-to-host, vendor, recipient device. */
    return vendorControlTransfer(0xC0, request, value, index, data, length, actualLength, address, timeoutMs);
}

bool CH32RVUsbHost::vendorControlOut(uint8_t request, uint16_t value, uint16_t index, const uint8_t *data,
                                     size_t length, uint8_t address, uint32_t timeoutMs)
{
    return vendorControlTransfer(0x40, request, value, index, const_cast<uint8_t *>(data), length, nullptr,
                                 address, timeoutMs);
}

bool CH32RVUsbHost::getStringDescriptor(uint8_t address, uint8_t index, char *out, size_t capacity)
{
    if (!out || !capacity) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    out[0] = '\0';
    const uint8_t daddr = resolve(address);
    if (!daddr) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    uint8_t *buf = ch32rv_usb_host_scratch;
    uint16_t got = 0;
    const uint8_t r = ch32rv_usb_host_control_sync(daddr, 0x80, 6, (uint16_t)(0x0300 | index), 0x0409, buf, 255,
                                                   &got, 1000);
    if (r != CH32RV_USBH_OK) {
        return fail(ch32rv_usb_host_result_error(r));
    }
    ascii_from_utf16(buf, got, out, capacity);
    return ok();
}

#else /* no host on this series */

bool CH32RVUsbHost::beginOn(const CH32RVUsbHcd *) { return fail(CH32RVUsbHostError::Unsupported); }
void CH32RVUsbHost::end() {}
bool CH32RVUsbHost::ready() const { return false; }
bool CH32RVUsbHost::suspend() { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::resume() { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::suspended() const { return false; }
void CH32RVUsbHost::onRemoteWakeup(RemoteWakeupCallback) {}
void CH32RVUsbHost::task() {}
uint32_t CH32RVUsbHost::droppedDeviceEvents() const { return 0; }
uint8_t CH32RVUsbHost::resolve(uint8_t) const { return 0; }
size_t CH32RVUsbHost::deviceCount() const { return 0; }
size_t CH32RVUsbHost::getDevices(CH32RVUsbHostDeviceInfo *, size_t) const { return 0; }
bool CH32RVUsbHost::getDevice(uint8_t, CH32RVUsbHostDeviceInfo &) const { return false; }
size_t CH32RVUsbHost::getInterfaces(uint8_t, CH32RVUsbHostInterfaceInfo *, size_t) { return 0; }
size_t CH32RVUsbHost::getEndpoints(uint8_t, CH32RVUsbHostEndpointInfo *, size_t) { return 0; }
bool CH32RVUsbHost::getHubInfo(uint8_t, CH32RVUsbHostHubInfo &) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::getHubPortStatus(uint8_t, uint8_t, uint16_t &, uint16_t &)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::setHubPortPower(uint8_t, uint8_t, bool) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::vendorControlTransfer(uint8_t, uint8_t, uint16_t, uint16_t, uint8_t *, size_t, size_t *,
                                          uint8_t, uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::vendorControlIn(uint8_t, uint16_t, uint16_t, uint8_t *, size_t, size_t *, uint8_t, uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::vendorControlOut(uint8_t, uint16_t, uint16_t, const uint8_t *, size_t, uint8_t, uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::getStringDescriptor(uint8_t, uint8_t, char *out, size_t capacity)
{
    if (out && capacity) {
        out[0] = '\0';
    }
    return fail(CH32RVUsbHostError::Unsupported);
}

#endif /* CH32RV_USB_HOST_STACK */

/* ------------------------------------------------------------- printing */
void ch32rvUsbHostPrintHex(const uint8_t *data, size_t length, Print &out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < length; i++) {
        if (i) {
            out.print(' ');
        }
        out.print(hex[data[i] >> 4]);
        out.print(hex[data[i] & 15]);
    }
}

static void print_hex16(Print &out, uint16_t v)
{
    static const char hex[] = "0123456789abcdef";
    for (int s = 12; s >= 0; s -= 4) {
        out.print(hex[(v >> s) & 15]);
    }
}

void ch32rvUsbHostPrint(const CH32RVUsbHostDeviceInfo &d, Print &out)
{
    out.print("device: address=");
    out.print(d.address);
    out.print(" vid=");
    print_hex16(out, d.vid);
    out.print(" pid=");
    print_hex16(out, d.pid);
    out.print(" class=");
    out.print(d.deviceClass, HEX);
    out.print(d.speed == CH32RV_USB_HOST_SPEED_LOW     ? " speed=low"
              : d.speed == CH32RV_USB_HOST_SPEED_HIGH  ? " speed=high"
              : CH32RV_USB_HOST_SUPERSPEED && d.speed == CH32RV_USB_HOST_SPEED_SUPER ? " speed=super"
                                                       : " speed=full");
    out.print(" manufacturer=\"");
    out.print(d.manufacturer);
    out.print("\" product=\"");
    out.print(d.product);
    out.print("\" serial=\"");
    out.print(d.serial);
    out.print("\" interfaces=");
    out.print(d.configurationInterfaceCount);
    if (d.isHub) {
        out.print(" hub");
    }
    out.println();
}

void ch32rvUsbHostPrint(const CH32RVUsbHostInterfaceInfo &i, Print &out)
{
    out.print("  interface ");
    out.print(i.number);
    out.print(" alt=");
    out.print(i.alternate);
    out.print(" class=");
    out.print(i.interfaceClass, HEX);
    out.print('/');
    out.print(i.interfaceSubClass, HEX);
    out.print('/');
    out.print(i.interfaceProtocol, HEX);
    out.print(" endpoints=");
    out.print(i.endpointCount);
    out.println(i.claimed ? " claimed" : "");
}

void ch32rvUsbHostPrint(const CH32RVUsbHostEndpointInfo &e, Print &out)
{
    static const char *const types[] = {"control", "isochronous", "bulk", "interrupt"};
    out.print("    endpoint 0x");
    out.print(e.address, HEX);
    out.print(" interface=");
    out.print(e.interfaceNumber);
    out.print(' ');
    out.print(types[e.attributes & 3]);
    out.print(" max_packet=");
    out.print(e.maxPacketSize);
    out.print(" interval=");
    out.println(e.interval);
}

void CH32RVUsbHost::printDeviceInfo(uint8_t address, Print &out)
{
    CH32RVUsbHostDeviceInfo d;
    if (!getDevice(address, d)) {
        out.println("device: none");
        return;
    }
    ch32rvUsbHostPrint(d, out);
    CH32RVUsbHostInterfaceInfo itfs[8];
    const size_t ni = getInterfaces(address, itfs, 8);
    for (size_t i = 0; i < ni; i++) {
        ch32rvUsbHostPrint(itfs[i], out);
    }
    CH32RVUsbHostEndpointInfo eps[12];
    const size_t ne = getEndpoints(address, eps, 12);
    for (size_t i = 0; i < ne; i++) {
        ch32rvUsbHostPrint(eps[i], out);
    }
}

void CH32RVUsbHost::printAllDeviceInfo(Print &out)
{
    CH32RVUsbHostDeviceInfo list[4];
    const size_t n = getDevices(list, 4);
    if (!n) {
        out.println("devices: none");
    }
    for (size_t i = 0; i < n; i++) {
        printDeviceInfo(list[i].address, out);
    }
}
