/* The device stack: EP0 and the standard requests, the descriptors, the
 * endpoints of the configuration and its alternate settings, transfers for the
 * classes, suspend / resume, and the reset into the factory USB bootloader.
 *
 * The controller driver (CH32RVUsbDcd.h) only records events in its
 * interrupt; everything here, and every class callback, runs in normal
 * context from ch32rv_usb_task() (loop() / delay()). One device runs at a
 * time. No descriptor set is kept in RAM: each is produced again when it is
 * asked for (CH32RVUsbDescriptorWriter), an EP0 packet at a time, so nothing
 * here is sized for the largest configuration. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#include "ch32rv_registers.h"
#include "ch32rv_deliver.h"
#include "ch32rv_service.h"
#include "CH32RVUsbDevicePort.h"

#define MAX_EP 16
#define MAX_ITF 16

struct CH32RVUsbDeviceCore {
    static CH32RVUsbDevice *device;
    static const CH32RVUsbDcd *dcd;

    /* From the interrupt. */
    static volatile uint8_t events;
    static uint8_t setup_raw[8];
    static volatile uint16_t done_in, done_out;       /* bit n: endpoint n finished */
    static volatile uint16_t len_in[MAX_EP], len_out[MAX_EP];

    /* Control transfer. */
    enum Ep0 : uint8_t { IDLE, DATA_IN, DATA_OUT, STATUS_IN, STATUS_OUT };
    static Ep0 ep0;
    static CH32RVUsbSetup req;
    static CH32RVUsbDeviceFunction *owner;            /* who answers the request */
    static bool answered, zlp;
    static uint16_t in_length;
    static uint16_t ep0_actual;                     /* data-stage bytes so far (controlActualBytes()) */
    /* A generated answer (controlInGenerated): where the next packet starts. */
    static void (*gen_fill)(void *context, uint32_t offset, uint8_t *buffer, uint16_t n);
    static void *gen_context;
    static uint16_t gen_offset, gen_chunk;
    static uint8_t pending_address;
    static uint8_t reply[2];

    /* The configuration. */
    static bool configured, suspended, remote_wakeup;
    static CH32RVUsbSpeed speed;
    static uint8_t ss_features;                      /* GET_STATUS bits 2-4: U1, U2, LTM enabled */
    static uint16_t ep0_mps;
    static uint8_t sel[6];                           /* SET_SEL: the exit latencies (not acted on) */
    static uint8_t alt[MAX_ITF];
    static CH32RVUsbDeviceFunction *ep_owner[MAX_EP];
    static uint16_t busy_in, busy_out, halt_in, halt_out;
    static volatile bool bootloader;

    static void reset_state();
    static void handle_setup();
    static bool standard(const CH32RVUsbSetup &s);
    static bool get_descriptor(const CH32RVUsbSetup &s);
    static void open_interface(uint8_t itf, uint8_t alternate, bool open);
    static void set_configuration(bool on);
    static CH32RVUsbDeviceFunction *interface_owner(uint8_t itf);
    static void ep0_done(bool in, uint16_t length);
    static void task();

    /* onBusEvent(): the bus events wait here, in order, for deliverEvents().
     * onAnyControlRequest() is ch32rv_usb_observe. */
    static CH32RVUsbDevice::BusEventCallback bus_event;
    static void observe(uint8_t stage, bool handled, uint16_t response);
    static CH32RVUsbBusEvent bus_queue[8];
    static uint8_t bus_first, bus_count;
    static uint32_t bus_dropped;
    static void bus(CH32RVUsbBusEvent e);
    static size_t deliver_bus();
    /* Both set by onBusEvent() only: without it neither the queue nor its
     * delivery is linked. */
    static void (*bus_record)(CH32RVUsbBusEvent e);
    static size_t (*bus_deliver)();
    static void note(CH32RVUsbBusEvent e)
    {
        if (bus_record) {
            bus_record(e);
        }
    }
    static size_t deliver();
};

CH32RVUsbDevice *CH32RVUsbDeviceCore::device;
const CH32RVUsbDcd *CH32RVUsbDeviceCore::dcd;
volatile uint8_t CH32RVUsbDeviceCore::events;
uint8_t CH32RVUsbDeviceCore::setup_raw[8];
volatile uint16_t CH32RVUsbDeviceCore::done_in, CH32RVUsbDeviceCore::done_out;
volatile uint16_t CH32RVUsbDeviceCore::len_in[MAX_EP], CH32RVUsbDeviceCore::len_out[MAX_EP];
CH32RVUsbDeviceCore::Ep0 CH32RVUsbDeviceCore::ep0;
CH32RVUsbSetup CH32RVUsbDeviceCore::req;
CH32RVUsbDeviceFunction *CH32RVUsbDeviceCore::owner;
bool CH32RVUsbDeviceCore::answered, CH32RVUsbDeviceCore::zlp;
uint16_t CH32RVUsbDeviceCore::in_length;
uint16_t CH32RVUsbDeviceCore::ep0_actual;
void (*CH32RVUsbDeviceCore::gen_fill)(void *, uint32_t, uint8_t *, uint16_t);
void *CH32RVUsbDeviceCore::gen_context;
uint16_t CH32RVUsbDeviceCore::gen_offset, CH32RVUsbDeviceCore::gen_chunk;
uint8_t CH32RVUsbDeviceCore::pending_address;
uint8_t CH32RVUsbDeviceCore::reply[2];
CH32RVUsbSpeed CH32RVUsbDeviceCore::speed;
uint8_t CH32RVUsbDeviceCore::ss_features;
uint16_t CH32RVUsbDeviceCore::ep0_mps = 64;
uint8_t CH32RVUsbDeviceCore::sel[6];
bool CH32RVUsbDeviceCore::configured, CH32RVUsbDeviceCore::suspended,
    CH32RVUsbDeviceCore::remote_wakeup;
uint8_t CH32RVUsbDeviceCore::alt[MAX_ITF];
CH32RVUsbDeviceFunction *CH32RVUsbDeviceCore::ep_owner[MAX_EP];
uint16_t CH32RVUsbDeviceCore::busy_in, CH32RVUsbDeviceCore::busy_out, CH32RVUsbDeviceCore::halt_in,
    CH32RVUsbDeviceCore::halt_out;
volatile bool CH32RVUsbDeviceCore::bootloader;
CH32RVUsbDevice::BusEventCallback CH32RVUsbDeviceCore::bus_event;
CH32RVUsbBusEvent CH32RVUsbDeviceCore::bus_queue[8];
uint8_t CH32RVUsbDeviceCore::bus_first, CH32RVUsbDeviceCore::bus_count;
uint32_t CH32RVUsbDeviceCore::bus_dropped;
void (*CH32RVUsbDeviceCore::bus_record)(CH32RVUsbBusEvent e);
size_t (*CH32RVUsbDeviceCore::bus_deliver)();



typedef CH32RVUsbDeviceCore Core;

void Core::observe(uint8_t stage, bool handled, uint16_t response)
{
    CH32RVUsbDeviceControlRequestInfo info;
    info.stage = stage;
    info.bmRequestType = req.bmRequestType;
    info.bRequest = req.bRequest;
    info.wValue = req.wValue;
    info.wIndex = req.wIndex;
    info.wLength = req.wLength;
    info.responseLength = response;
    info.handled = handled;
    ch32rv_usb_observe(info);
}

void (*ch32rv_usb_observe)(const CH32RVUsbDeviceControlRequestInfo &info);

void Core::bus(CH32RVUsbBusEvent e)
{
    if (!bus_event) {
        return;
    }
    if (bus_count == sizeof bus_queue / sizeof bus_queue[0]) {
        bus_dropped++;
        return;
    }
    bus_queue[(uint8_t)(bus_first + bus_count) % (sizeof bus_queue / sizeof bus_queue[0])] = e;
    bus_count++;
}

/* The sketch's side: the bus events in order, then each function's own
 * notifications. A callback may call delay(), which runs the stack and may
 * record more; those wait for the next delivery. */
size_t Core::deliver_bus()
{
    size_t ran = 0;
    for (uint8_t n = bus_count; n && bus_count; n--) {
        const CH32RVUsbBusEvent e = bus_queue[bus_first];
        bus_first = (uint8_t)((bus_first + 1) % (sizeof bus_queue / sizeof bus_queue[0]));
        bus_count--;
        if (bus_event) {
            bus_event(e);
            ran++;
        }
    }
    return ran;
}

size_t Core::deliver()
{
    size_t ran = bus_deliver ? bus_deliver() : 0;
    if (!device) {
        return ran;
    }
    for (CH32RVUsbDeviceFunction *f = device->_first; f; f = f->_next) {
        if (f->_attached && f->_deliver) {
            ran += f->_deliver(*f);
        }
    }
    return ran;
}

#define EV_RESET   0x01
#define EV_SETUP   0x02
#define EV_SUSPEND 0x04
#define EV_RESUME  0x08

/* One EP0 packet of a generated answer. */
static uint8_t ep0_packet[ch32rv_usb_series_can(CH32RVUsbSpeed::Super) ? 512 : 64] __attribute__((aligned(4)));
static uint8_t desc_device[18];
static uint8_t desc_string[2 + 2 * 32] __attribute__((aligned(2)));   /* stringDescriptor() hands it out as uint16_t */
static void build_device_descriptor(const CH32RVUsbDeviceConfig &c);

/* A SuperSpeed device needs a BOS even without the platform descriptors: the
 * USB 2.0 extension (LPM) and the SuperSpeed USB capability (all speeds,
 * functionality from full speed, U1 / U2 exit latencies left to the driver's
 * defaults). */
static const uint8_t ss_bos[] = {
    5, 15, 22, 0, 2,
    7, 0x10, 0x02, 0x02, 0, 0, 0,
    10, 0x10, 0x03, 0x00, 0x0E, 0x00, 0x01, 0x0A, 0xFF, 0x07,
};
const uint8_t ch32rv_usb_ss_caps[17] = {
    7, 0x10, 0x02, 0x02, 0, 0, 0,
    10, 0x10, 0x03, 0x00, 0x0E, 0x00, 0x01, 0x0A, 0xFF, 0x07,
};
bool ch32rv_usb_bos_super;
uint32_t ch32rv_usb_hse_hz = 8000000;

/* The BOS's length (0: none) - the platform's, or the SuperSpeed one. */
static uint16_t bos_length(void)
{
    if (ch32rv_usb_platform.bosLength) {
        return ch32rv_usb_platform.bosLength;
    }
    return ch32rv_usb_dcd_can(*Core::dcd, CH32RVUsbSpeed::Super) ? (uint16_t)sizeof ss_bos : 0;
}

static void (*written_by)(CH32RVUsbDescriptorWriter &w);

bool ch32rv_usb_control_in_written(CH32RVUsbDevice &device, uint16_t length,
                                   void (*write)(CH32RVUsbDescriptorWriter &w))
{
    written_by = write;
    return device.controlInGenerated(length, [](void *, uint32_t offset, uint8_t *buffer, uint16_t n) {
        CH32RVUsbDescriptorWriter w;
        w.out = buffer;
        w.skip = offset;
        w.room = n;
        written_by(w);
    }, nullptr);
}

/* ------------------------------------------------------------ from the driver's interrupt */
extern "C" void ch32rv_usbd_isr_reset(void)
{
    Core::events = (uint8_t)((Core::events & ~EV_SETUP) | EV_RESET);
    Core::done_in = Core::done_out = 0;
}

extern "C" void ch32rv_usbd_isr_setup(const uint8_t setup[8])
{
    memcpy(Core::setup_raw, setup, 8);
    Core::events |= EV_SETUP;
    Core::done_in = (uint16_t)(Core::done_in & ~1u);      /* EP0's old stages are void */
    Core::done_out = (uint16_t)(Core::done_out & ~1u);
}

extern "C" void ch32rv_usbd_isr_xfer(uint8_t address, uint16_t length)
{
    const uint8_t ep = address & 0x0F;
    if (address & 0x80) {
        Core::len_in[ep] = length;
        Core::done_in |= (uint16_t)(1u << ep);
    } else {
        Core::len_out[ep] = length;
        Core::done_out |= (uint16_t)(1u << ep);
    }
}

extern "C" void ch32rv_usbd_isr_suspend(void) { Core::events |= EV_SUSPEND; }
extern "C" void ch32rv_usbd_isr_resume(void) { Core::events |= EV_RESUME; }

/* ------------------------------------------------------------ the configuration */
CH32RVUsbDeviceFunction *Core::interface_owner(uint8_t itf)
{
    for (CH32RVUsbDeviceFunction *f = device->_first; f; f = f->_next) {
        if (itf >= f->_interface && itf < f->_interface + f->_type.interfaces) {
            return f;
        }
    }
    return nullptr;
}

/* Open (or close) the endpoints of one alternate setting of an interface, as
 * the configuration descriptor lists them: produced again and visited. */
namespace {
struct OpenWalk {
    uint8_t itf, alternate;
    bool open, mine;
    CH32RVUsbDeviceFunction *owner;
    CH32RVUsbDescriptorWriter *writer;
};
}  // namespace

void Core::open_interface(uint8_t itf, uint8_t alternate, bool open)
{
    CH32RVUsbDescriptorWriter w;
    OpenWalk walk = {itf, alternate, open, false, interface_owner(itf), &w};
    w.context = &walk;
    w.visit = [](void *context, const uint8_t *d, uint8_t n) {
        OpenWalk &k = *(OpenWalk *)context;
        if (n >= 4 && d[1] == 4) {                       /* INTERFACE */
            k.mine = d[2] == k.itf && d[3] == k.alternate;
            if (d[2] > k.itf) {
                k.writer->done = true;                   /* interfaces come in order: nothing more */
            }
        } else if (n >= 6 && d[1] == 5 && k.mine) {      /* ENDPOINT */
            const uint8_t address = d[2], ep = address & 0x0F;
            const uint16_t mps = (uint16_t)((d[4] | d[5] << 8) & 0x7FF);
            const uint16_t bit = (uint16_t)(1u << ep);
            if (ep >= MAX_EP) {
                return;
            }
            if (k.open) {
                dcd->epOpen(address, (uint8_t)(d[3] & 3), mps);
                ep_owner[ep] = k.owner;
            } else {
                dcd->epClose(address);
            }
            if (address & 0x80) {
                busy_in &= (uint16_t)~bit;
                halt_in &= (uint16_t)~bit;
            } else {
                busy_out &= (uint16_t)~bit;
                halt_out &= (uint16_t)~bit;
            }
        }
    };
    device->buildConfiguration(w, speed, 2);
}

void Core::set_configuration(bool on)
{
    dcd->epCloseAll();
    busy_in = busy_out = halt_in = halt_out = 0;
    memset(ep_owner, 0, sizeof ep_owner);
    memset(alt, 0, sizeof alt);
    if (configured) {
        configured = false;
        for (CH32RVUsbDeviceFunction *f = device->_first; f; f = f->_next) {
            f->usbReset();
        }
    }
    if (!on) {
        return;
    }
    const uint8_t interfaces = device->interfaceCount();
    for (uint8_t itf = 0; itf < interfaces && itf < MAX_ITF; itf++) {
        open_interface(itf, 0, true);
    }
    configured = true;
    for (CH32RVUsbDeviceFunction *f = device->_first; f; f = f->_next) {
        f->usbConfigured(speed);
    }
}

void Core::reset_state()
{
    if (configured) {
        set_configuration(false);
    }
    ep0 = IDLE;
    pending_address = 0;
    suspended = false;
    remote_wakeup = false;
    ss_features = 0;
    speed = dcd->speed();
    ep0_mps = ch32rv_usb_at(speed, CH32RVUsbSpeed::Super) ? 512 : 64;
    build_device_descriptor(device->_config);
}

/* ------------------------------------------------------------ descriptors */
static void build_device_descriptor(const CH32RVUsbDeviceConfig &c)
{
    /* bcdUSB 3.20 at SuperSpeed (EP0 then 2^9 bytes); otherwise 2.10 when
     * there is a BOS - a host reads it only from a 2.1 device. IAD: CDC,
     * MIDI, audio and video are interfaces bound together. */
    const uint16_t bos_size = bos_length();
    const bool ss = ch32rv_usb_at(Core::speed, CH32RVUsbSpeed::Super);
    const uint16_t bcd = ss ? 0x0320 : bos_size ? 0x0210 : 0x0200;
    const uint8_t d[18] = {18, 1, (uint8_t)bcd, (uint8_t)(bcd >> 8), 0xEF, 0x02, 0x01, (uint8_t)(ss ? 9 : 64),
                           (uint8_t)c.vid, (uint8_t)(c.vid >> 8), (uint8_t)c.pid, (uint8_t)(c.pid >> 8),
                           (uint8_t)c.deviceVersion, (uint8_t)(c.deviceVersion >> 8), 1, 2, 3, 1};
    memcpy(desc_device, d, sizeof d);
}

static uint16_t ascii_string(const char *s)
{
    uint8_t n = 0;
    while (s && s[n] && n < 32) {
        desc_string[2 + 2 * n] = (uint8_t)s[n];
        desc_string[3 + 2 * n] = 0;
        n++;
    }
    desc_string[0] = (uint8_t)(2 + 2 * n);
    desc_string[1] = 3;
    return desc_string[0];
}

/* String descriptors: UTF-16LE built on request, ASCII only. Index 3 without
 * a configured serial number is the chip's 64-bit unique ID in hex, UNIID1's
 * eight digits then UNIID2's: the order ch32rv, WCH-Link's chip info and the
 * OEP unit names use, so the same chip reads the same there and here. */
static uint16_t string_descriptor(uint8_t index)
{
    const CH32RVUsbDeviceConfig &c = Core::device->config();
    if (index == 0) {
        static const uint8_t lang[4] = {4, 3, 0x09, 0x04};
        memcpy(desc_string, lang, 4);
        return 4;
    }
    if (index == 1) {
        return ascii_string(c.manufacturer);
    }
    if (index == 2) {
        return ascii_string(c.product);
    }
    if (index == 3 && c.serialNumber) {
        return ascii_string(c.serialNumber);
    }
    if (index == 3) {
        static const char hex[] = "0123456789ABCDEF";
        char uid[17];
        const uint32_t lo = *(volatile const uint32_t *)CH32RV_ESIG_UNIID1_ADDR;
        const uint32_t hi = *(volatile const uint32_t *)CH32RV_ESIG_UNIID2_ADDR;
        for (uint8_t n = 0; n < 8; n++) {
            uid[n] = hex[(lo >> (28 - 4 * n)) & 0xF];
            uid[n + 8] = hex[(hi >> (28 - 4 * n)) & 0xF];
        }
        uid[16] = 0;
        return ascii_string(uid);
    }
    if (index >= CH32RV_USB_EXTRA_STRING) {
        const char *text = ch32rv_usb_extra_string ? ch32rv_usb_extra_string(index) : nullptr;
        return text ? ascii_string(text) : 0;
    }
    const CH32RVUsbDeviceFunction *f = Core::device->function((uint8_t)(index - CH32RV_USB_FIRST_FUNCTION_STRING));
    const char *name = f ? f->interfaceName() : nullptr;
    return name ? ascii_string(name) : 0;
}

bool Core::get_descriptor(const CH32RVUsbSetup &s)
{
    switch (s.wValue >> 8) {
    case 1:                                              /* DEVICE */
        return device->controlIn(desc_device, sizeof desc_device);
    case 2:                                              /* CONFIGURATION */
    case 7: {                                            /* OTHER_SPEED_CONFIGURATION */
        /* Only a high-speed capable device running at full or high speed
         * describes the other one (USB 2.0 9.6.2, 9.6.4). */
        const bool other = (s.wValue >> 8) == 7;
        if (other && (!ch32rv_usb_dcd_can(*dcd, CH32RVUsbSpeed::High) || ch32rv_usb_at(speed, CH32RVUsbSpeed::Super))) {
            return false;
        }
        static struct {
            CH32RVUsbSpeed speed;
            uint8_t type;
        } which;
        which.speed = !other ? speed : ch32rv_usb_at(speed, CH32RVUsbSpeed::High) ? CH32RVUsbSpeed::Full
                                                                                   : CH32RVUsbSpeed::High;
        which.type = (uint8_t)(s.wValue >> 8);
        CH32RVUsbDescriptorWriter count;
        const uint16_t n = device->buildConfiguration(count, which.speed, which.type);
        return n && device->controlInGenerated(n, [](void *context, uint32_t offset, uint8_t *buffer, uint16_t length) {
            auto &c = *(decltype(which) *)context;
            CH32RVUsbDescriptorWriter w;
            w.out = buffer;
            w.skip = offset;
            w.room = length;
            Core::device->buildConfiguration(w, c.speed, c.type);
        }, &which);
    }
    case 3: {                                            /* STRING */
        const uint16_t n = string_descriptor((uint8_t)s.wValue);
        return n && device->controlIn(desc_string, n);
    }
    case 6:                                              /* DEVICE_QUALIFIER: high-speed capable only */
        if (!ch32rv_usb_dcd_can(*dcd, CH32RVUsbSpeed::High) || ch32rv_usb_at(speed, CH32RVUsbSpeed::Super)) {
            return false;
        } else {
            static uint8_t q[10];
            memcpy(q, desc_device, 8);
            q[0] = 10;
            q[1] = 6;
            q[8] = 1;
            q[9] = 0;
            return device->controlIn(q, sizeof q);
        }
    case 15:                                             /* BOS */
        if (ch32rv_usb_platform.bosLength) {
            return ch32rv_usb_control_in_written(*device, ch32rv_usb_platform.bosLength, ch32rv_usb_platform.bosWrite);
        }
        return bos_length() && device->controlIn(ss_bos, sizeof ss_bos);
    }
    return false;
}

/* ------------------------------------------------------------ requests */
bool Core::standard(const CH32RVUsbSetup &s)
{
    const uint8_t recipient = s.bmRequestType & 0x1F;
    if (recipient == 0) {                                /* device */
        switch (s.bRequest) {
        case 6: return get_descriptor(s);                /* GET_DESCRIPTOR */
        case 5:                                          /* SET_ADDRESS: after the status stage */
            pending_address = (uint8_t)(s.wValue & 0x7F);
            return device->controlStatus();
        case 9:                                          /* SET_CONFIGURATION */
            if (s.wValue > 1) {
                return false;
            }
            set_configuration(s.wValue == 1);
            return device->controlStatus();
        case 8:                                          /* GET_CONFIGURATION */
            reply[0] = configured ? 1 : 0;
            return device->controlIn(reply, 1);
        case 0:                                          /* GET_STATUS */
            reply[0] = (uint8_t)((device->_config.selfPowered ? 1 : 0) | (remote_wakeup ? 2 : 0) | ss_features);
            reply[1] = 0;
            return device->controlIn(reply, 2);
        case 1:                                          /* CLEAR_FEATURE */
        case 3:                                          /* SET_FEATURE */
            if (s.wValue == 1 && !ch32rv_usb_at(speed, CH32RVUsbSpeed::Super) &&   /* DEVICE_REMOTE_WAKEUP */
                device->_config.remoteWakeupSupported) {
                remote_wakeup = s.bRequest == 3;
                return device->controlStatus();
            }
            if (s.wValue >= 48 && s.wValue <= 50 && ch32rv_usb_at(speed, CH32RVUsbSpeed::Super)) {   /* U1, U2, LTM */
                const uint8_t bit = (uint8_t)(4u << (s.wValue - 48));
                ss_features = (uint8_t)(s.bRequest == 3 ? ss_features | bit : ss_features & ~bit);
                return device->controlStatus();
            }
            return false;
        case 0x30:                                       /* SET_SEL (SuperSpeed) */
            return ch32rv_usb_at(speed, CH32RVUsbSpeed::Super) && device->controlOut(sel, sizeof sel);
        case 0x31:                                       /* SET_ISOCH_DELAY (SuperSpeed) */
            return ch32rv_usb_at(speed, CH32RVUsbSpeed::Super) && device->controlStatus();
        }
        return false;
    }
    if (recipient == 1) {                                /* interface */
        const uint8_t itf = (uint8_t)s.wIndex;
        CH32RVUsbDeviceFunction *f = interface_owner(itf);
        if (!f || !configured || itf >= MAX_ITF) {
            return false;
        }
        switch (s.bRequest) {
        case 0:                                          /* GET_STATUS */
            reply[0] = reply[1] = 0;
            return device->controlIn(reply, 2);
        case 1:                                          /* CLEAR_FEATURE */
        case 3:                                          /* SET_FEATURE: FUNCTION_SUSPEND (SuperSpeed) */
            if (s.wValue != 0 || !ch32rv_usb_at(speed, CH32RVUsbSpeed::Super)) {
                return false;
            }
            return device->controlStatus();
        case 10:                                         /* GET_INTERFACE */
            reply[0] = alt[itf];
            return device->controlIn(reply, 1);
        case 11:                                         /* SET_INTERFACE */
            open_interface(itf, alt[itf], false);
            open_interface(itf, (uint8_t)s.wValue, true);
            if (!f->usbSetInterface(itf, (uint8_t)s.wValue)) {
                open_interface(itf, (uint8_t)s.wValue, false);
                open_interface(itf, alt[itf], true);
                return false;
            }
            alt[itf] = (uint8_t)s.wValue;
            return device->controlStatus();
        case 6:                                          /* GET_DESCRIPTOR (HID report and such) */
            owner = f;
            return f->usbControl(CH32RVUsbControlStage::Setup, s) && answered;
        }
        return false;
    }
    if (recipient == 2) {                                /* endpoint */
        const uint8_t address = (uint8_t)s.wIndex, ep = address & 0x0F;
        if (ep >= MAX_EP || (ep && !ep_owner[ep])) {
            return false;
        }
        const uint16_t bit = (uint16_t)(1u << ep);
        uint16_t &halt = (address & 0x80) ? halt_in : halt_out;
        switch (s.bRequest) {
        case 0:                                          /* GET_STATUS */
            reply[0] = (halt & bit) ? 1 : 0;
            reply[1] = 0;
            return device->controlIn(reply, 2);
        case 1:                                          /* CLEAR_FEATURE (ENDPOINT_HALT) */
        case 3:                                          /* SET_FEATURE */
            if (s.wValue != 0 || ep == 0) {
                return false;
            }
            if (s.bRequest == 3) {
                halt |= bit;
                dcd->stall(address, true);
            } else {
                halt &= (uint16_t)~bit;
                dcd->stall(address, false);
                ep_owner[ep]->usbHaltCleared(address);
            }
            return device->controlStatus();
        }
        return false;
    }
    return false;
}

void Core::handle_setup()
{
    CH32RVUsbSetup &s = req;
    s.bmRequestType = setup_raw[0];
    s.bRequest = setup_raw[1];
    s.wValue = (uint16_t)(setup_raw[2] | setup_raw[3] << 8);
    s.wIndex = (uint16_t)(setup_raw[4] | setup_raw[5] << 8);
    s.wLength = (uint16_t)(setup_raw[6] | setup_raw[7] << 8);
    owner = nullptr;
    answered = false;
    gen_fill = nullptr;
    in_length = 0;
    ep0_actual = 0;
    ep0 = IDLE;
    bool ok = false;
    const uint8_t type = s.bmRequestType & 0x60, recipient = s.bmRequestType & 0x1F;
    if (type == 0x00) {
        ok = standard(s);
    } else if (recipient == 1) {
        owner = interface_owner((uint8_t)s.wIndex);
        ok = owner && owner->usbControl(CH32RVUsbControlStage::Setup, s) && answered;
    } else if (recipient == 2) {
        const uint8_t ep = s.wIndex & 0x0F;
        owner = ep < MAX_EP ? ep_owner[ep] : nullptr;
        ok = owner && owner->usbControl(CH32RVUsbControlStage::Setup, s) && answered;
    } else if (type == 0x40 && ch32rv_usb_platform.request) {
        /* A vendor request to the device: the WebUSB / MS OS 2.0 descriptors
         * and CH32RVUsbDeviceVendor::onControlRequest. */
        ok = ch32rv_usb_platform.request(CH32RVUsbControlStage::Setup, s) && answered;
    }
    if (ch32rv_usb_observe && type == 0x40) {
        observe(CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP, ok, 0);
    }
    if (!ok) {
        dcd->ep0Stall();
        ep0 = IDLE;
    }
}

void Core::ep0_done(bool in, uint16_t length)
{
    if (in && ep0 == DATA_IN && gen_fill) {
        gen_offset = (uint16_t)(gen_offset + gen_chunk);
        if (gen_offset < in_length) {                    /* the next packet of a generated answer */
            const uint16_t left = (uint16_t)(in_length - gen_offset);
            gen_chunk = left < ep0_mps ? left : ep0_mps;
            gen_fill(gen_context, gen_offset, ep0_packet, gen_chunk);
            dcd->xfer(0x80, ep0_packet, gen_chunk);
            return;
        }
        gen_fill = nullptr;
    }
    if (in && ep0 == DATA_IN) {
        ep0_actual = in_length;
        if (zlp) {                                       /* a full last packet shorter than asked */
            zlp = false;
            dcd->xfer(0x80, nullptr, 0);
            return;
        }
        ep0 = STATUS_OUT;
        dcd->xfer(0x00, nullptr, 0);
    } else if (!in && ep0 == DATA_OUT) {
        ep0_actual = length;
        bool ok = true;
        if (owner) {
            ok = owner->usbControl(CH32RVUsbControlStage::Data, req);
        } else if (ch32rv_usb_platform.request) {
            ok = ch32rv_usb_platform.request(CH32RVUsbControlStage::Data, req);
        }
        if (!ok) {
            dcd->ep0Stall();
            ep0 = IDLE;
            return;
        }
        ep0 = STATUS_IN;
        dcd->xfer(0x80, nullptr, 0);
    } else if ((in && ep0 == STATUS_IN) || (!in && ep0 == STATUS_OUT)) {
        ep0 = IDLE;
        if (pending_address) {
            dcd->setAddress(pending_address);
            pending_address = 0;
        }
        if (owner) {
            owner->usbControl(CH32RVUsbControlStage::Ack, req);
        } else if ((req.bmRequestType & 0x60) == 0x40 && ch32rv_usb_platform.request) {
            ch32rv_usb_platform.request(CH32RVUsbControlStage::Ack, req);
        }
        if (ch32rv_usb_observe) {
            observe(CH32RV_USB_DEVICE_CONTROL_STAGE_ACK, true, (req.bmRequestType & 0x80) ? in_length : 0);
        }
    }
}

/* ------------------------------------------------------------ the task */
void Core::task()
{
    if (dcd->poll) {
        dcd->poll();
    }
    noInterrupts();
    const uint8_t e = events;
    const uint16_t din = done_in, dout = done_out;
    uint8_t s[8];
    memcpy(s, setup_raw, 8);
    events = 0;
    done_in = done_out = 0;
    interrupts();

    bool was_configured = configured;
    if (e & EV_RESET) {
        reset_state();
        was_configured = false;
        note(CH32RVUsbBusEvent::Reset);
    }
    if (e & EV_SUSPEND) {
        suspended = true;
        note(CH32RVUsbBusEvent::Suspended);
    }
    if (e & EV_RESUME) {
        suspended = false;
        note(CH32RVUsbBusEvent::Resumed);
    }
    if (e & EV_SETUP) {
        memcpy(setup_raw, s, 8);
        handle_setup();
    }
    /* SET_CONFIGURATION answered just now. */
    if (configured != was_configured) {
        note(configured ? CH32RVUsbBusEvent::Configured : CH32RVUsbBusEvent::Unconfigured);
    }
    if (dout & 1u) {
        ep0_done(false, len_out[0]);
    }
    if (din & 1u) {
        ep0_done(true, len_in[0]);
    }
    for (uint8_t ep = 1; ep < MAX_EP; ep++) {
        const uint16_t bit = (uint16_t)(1u << ep);
        if ((dout & bit) && (busy_out & bit)) {
            busy_out &= (uint16_t)~bit;
            if (ep_owner[ep]) {
                ep_owner[ep]->usbTransferred(ep, len_out[ep]);
            }
        }
        if ((din & bit) && (busy_in & bit)) {
            busy_in &= (uint16_t)~bit;
            if (ep_owner[ep]) {
                ep_owner[ep]->usbTransferred((uint8_t)(0x80 | ep), len_in[ep]);
            }
        }
    }
    if (configured && !suspended) {
        for (CH32RVUsbDeviceFunction *f = device->_first; f; f = f->_next) {
            f->usbPoll();
        }
    }
}

/* main() and delay() call this (Arduino.h weak hook); defining it here means
 * a sketch without a device links none of it. A callback that calls delay()
 * would re-enter; the guard makes that a no-op instead. */
extern "C" void ch32rv_usb_task(void)
{
    static volatile bool busy;
    if (!Core::device || !Core::device->started() || busy) {
        return;
    }
    /* Leave only once the reply to the request that asked for it is out: the
     * status stage of a DFU_DETACH, the host's close after a 1200 bps touch. */
    static uint32_t leave_at;
    static bool leaving;
    if (Core::bootloader) {
        /* A flag of its own: a timestamp forced odd to mean "set" can be a
         * millisecond ahead of millis(), and the difference then wraps and
         * leaves at once, before the reply is out. */
        if (!leaving) {
            leaving = true;
            leave_at = millis();
        } else if ((uint32_t)(millis() - leave_at) >= 20u) {
            CH32RVUsbDevice::enterBootloader();
        }
    }
    busy = true;
    Core::task();
    busy = false;
    if (void (*f)(void) = ch32rv_usb_deferred) {
        ch32rv_usb_deferred = nullptr;
        f();
    }
}

void (*volatile ch32rv_usb_deferred)(void);

/* ------------------------------------------------------------ the device's API */
/* The stack's system service (ch32rv_service.h): begin() attaches it. end()
 * leaves it, and the delivery below, in the round - both return at once
 * without a device - rather than link two detaches into a part that calls
 * end() only to restart (DFU detach, restartWithOnly()). */
static ch32rv_service_t usb_service = CH32RV_SERVICE_INIT(ch32rv_usb_task);
/* And the delivery of the sketch's notifications (ch32rv_deliver.h). */
static ch32rv_delivery_t usb_delivery = CH32RV_DELIVERY_INIT(Core::deliver);

bool CH32RVUsbDevice::begin(const CH32RVUsbDeviceConfig &config)
{
    if (_started) {
        _error = CH32RVUsbDeviceError::AlreadyStarted;
        return false;
    }
#ifndef CH32RV_USB_NO_DEFAULT_DFU
    if (config.dfuRuntime) {
        /* Best effort: a refusal here (no room) is not the sketch's error. */
        const CH32RVUsbDeviceError before = _error;
        CH32RVUsbDeviceDfuRuntime::attachDefault(*this);
        _error = before;
    }
#endif
    const CH32RVUsbDcd *dcd = port_dcd(config.controller);
    if (!dcd) {
        _error = config.controller == CH32RVUsbController::Auto ? CH32RVUsbDeviceError::Unsupported
                                                                : CH32RVUsbDeviceError::PortUnavailable;
        return false;
    }
    _config = config;
    _endpoints = dcd->endpoints;
    _maxPacket = dcd->maxPacket;
    /* Lay the configuration out once for each speed, so a function that does
     * not fit fails here rather than at enumeration. */
    Core::dcd = dcd;
    for (uint8_t sp = 0; sp < 3; sp++) {
        CH32RVUsbDescriptorWriter count;
        if (ch32rv_usb_dcd_can(*dcd, (CH32RVUsbSpeed)sp) && !buildConfiguration(count, (CH32RVUsbSpeed)sp, 2)) {
            return false;
        }
    }
    /* The BOS and the Microsoft OS 2.0 set need the interface numbers. */
    ch32rv_usb_bos_super = ch32rv_usb_dcd_can(*dcd, CH32RVUsbSpeed::Super);
    if (ch32rv_usb_platform.build && !ch32rv_usb_platform.build(*this)) {
        _error = CH32RVUsbDeviceError::InvalidArgument;
        return false;
    }
    ch32rv_usb_hse_hz = config.hseHz;
    if (!dcd->open()) {
        _error = CH32RVUsbDeviceError::Busy;
        return false;
    }
    Core::device = this;
    Core::dcd = dcd;
    Core::configured = false;
    Core::bootloader = false;
    noInterrupts();
    Core::events = 0;
    Core::done_in = Core::done_out = 0;
    interrupts();
    Core::reset_state();
    _started = true;
    _error = CH32RVUsbDeviceError::None;
    /* delay() and the return of loop() run the stack from here on: the sketch
     * never calls a task() for enumeration and transfers to happen. */
    Core::bus_first = Core::bus_count = 0;
    Core::bus_dropped = 0;
    ch32rv_service_attach(&usb_service);
    ch32rv_delivery_attach(&usb_delivery);
    dcd->connect(true);
    return true;
}

void CH32RVUsbDevice::end()
{
    if (!_started || Core::device != this) {
        return;
    }
    Core::dcd->connect(false);
    /* Cleared before the functions forget their transfers, so a borrowed
     * buffer is handed back as Stopped rather than BusReset. */
    _started = false;
    if (Core::configured) {
        Core::set_configuration(false);
    }
    Core::dcd->close();
    Core::device = nullptr;
}

bool CH32RVUsbDevice::restartWithOnly(CH32RVUsbDeviceFunction &only)
{
    end();
    const uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 50u) {           /* long enough for the host to see it go */
    }
    only._next = nullptr;
    _first = &only;
    _count = 1;
    only._instance = 0;
    CH32RVUsbDeviceConfig c = _config;
    c.dfuRuntime = false;
    return begin(c);
}

bool CH32RVUsbDevice::ready() const
{
    return _started && Core::device == this && Core::configured;
}

uint8_t CH32RVUsbDevice::interfaceCount() const
{
    uint8_t n = 0;
    for (const CH32RVUsbDeviceFunction *f = _first; f; f = f->_next) {
        n = (uint8_t)(n + f->_type.interfaces);
    }
    return n;
}

bool CH32RVUsbDevice::suspended() const
{
    return _started && Core::suspended;
}

bool CH32RVUsbDevice::remoteWakeupEnabled() const
{
    return _started && Core::remote_wakeup;
}

bool CH32RVUsbDevice::remoteWakeup()
{
    if (!_started || !Core::suspended || !Core::remote_wakeup) {
        return false;
    }
    Core::dcd->remoteWakeup();
    return true;
}

CH32RVUsbSpeed CH32RVUsbDevice::speed() const
{
    return _started ? Core::speed : CH32RVUsbSpeed::Full;
}

void CH32RVUsbDevice::requestBootloader()
{
    Core::bootloader = true;
}

void CH32RVUsbDevice::task()
{
    ch32rv_usb_task();
}

bool CH32RVUsbDevice::transfer(uint8_t address, void *buffer, uint16_t length)
{
    const uint8_t ep = address & 0x0F;
    if (!ready() || ep == 0 || ep >= MAX_EP || !Core::ep_owner[ep]) {
        return false;
    }
    const uint16_t bit = (uint16_t)(1u << ep);
    uint16_t &busy = (address & 0x80) ? Core::busy_in : Core::busy_out;
    const uint16_t halt = (address & 0x80) ? Core::halt_in : Core::halt_out;
    if ((busy & bit) || (halt & bit)) {
        return false;
    }
    busy |= bit;
    if (!Core::dcd->xfer(address, (uint8_t *)buffer, length)) {
        busy &= (uint16_t)~bit;
        return false;
    }
    return true;
}

bool CH32RVUsbDevice::transferBusy(uint8_t address) const
{
    const uint8_t ep = address & 0x0F;
    if (ep >= MAX_EP) {
        return false;
    }
    return (((address & 0x80) ? Core::busy_in : Core::busy_out) >> ep) & 1u;
}

void CH32RVUsbDevice::abort(uint8_t address)
{
    const uint8_t ep = address & 0x0F;
    if (!_started || ep == 0 || ep >= MAX_EP) {
        return;
    }
    const uint16_t bit = (uint16_t)(1u << ep);
    Core::dcd->abort(address);
    noInterrupts();
    if (address & 0x80) {
        Core::done_in = (uint16_t)(Core::done_in & ~bit);   /* a completion not yet handled is void too */
        Core::busy_in = (uint16_t)(Core::busy_in & ~bit);
    } else {
        Core::done_out = (uint16_t)(Core::done_out & ~bit);
        Core::busy_out = (uint16_t)(Core::busy_out & ~bit);
    }
    interrupts();
}

void CH32RVUsbDevice::stall(uint8_t address)
{
    const uint8_t ep = address & 0x0F;
    if (!_started || ep == 0 || ep >= MAX_EP) {
        return;
    }
    ((address & 0x80) ? Core::halt_in : Core::halt_out) |= (uint16_t)(1u << ep);
    Core::dcd->stall(address, true);
}

bool CH32RVUsbDevice::stalled(uint8_t address) const
{
    const uint8_t ep = address & 0x0F;
    if (ep >= MAX_EP) {
        return false;
    }
    return (((address & 0x80) ? Core::halt_in : Core::halt_out) >> ep) & 1u;
}

bool CH32RVUsbDevice::controlIn(const void *data, uint16_t length)
{
    if (Core::answered) {
        return false;
    }
    Core::answered = true;
    const uint16_t n = length < Core::req.wLength ? length : Core::req.wLength;
    Core::in_length = n;
    Core::zlp = n < Core::req.wLength && n && (n % Core::ep0_mps) == 0;
    Core::ep0 = Core::DATA_IN;
    return Core::dcd->xfer(0x80, (uint8_t *)data, n);
}

bool CH32RVUsbDevice::controlInGenerated(uint16_t length,
                                         void (*fill)(void *context, uint32_t offset, uint8_t *buffer, uint16_t n),
                                         void *context)
{
    if (Core::answered || !fill) {
        return false;
    }
    Core::answered = true;
    const uint16_t n = length < Core::req.wLength ? length : Core::req.wLength;
    Core::in_length = n;
    Core::zlp = n < Core::req.wLength && n && (n % Core::ep0_mps) == 0;
    Core::ep0 = Core::DATA_IN;
    if (!n) {
        return Core::dcd->xfer(0x80, nullptr, 0);
    }
    Core::gen_fill = fill;
    Core::gen_context = context;
    Core::gen_offset = 0;
    Core::gen_chunk = n < Core::ep0_mps ? n : Core::ep0_mps;
    fill(context, 0, ep0_packet, Core::gen_chunk);
    return Core::dcd->xfer(0x80, ep0_packet, Core::gen_chunk);
}

uint16_t CH32RVUsbDevice::controlActualBytes() const
{
    return Core::ep0_actual;
}

bool CH32RVUsbDevice::controlOut(void *buffer, uint16_t length)
{
    if (Core::answered) {
        return false;
    }
    Core::answered = true;
    const uint16_t n = length < Core::req.wLength ? length : Core::req.wLength;
    if (n == 0) {
        Core::ep0 = Core::STATUS_IN;
        return Core::dcd->xfer(0x80, nullptr, 0);
    }
    Core::ep0 = Core::DATA_OUT;
    return Core::dcd->xfer(0x00, (uint8_t *)buffer, n);
}

bool CH32RVUsbDevice::controlStatus()
{
    if (Core::answered) {
        return false;
    }
    Core::answered = true;
    Core::ep0 = Core::STATUS_IN;
    return Core::dcd->xfer(0x80, nullptr, 0);
}

/* Leave the bus first so the host sees the device go away now, not at the
 * next transfer timeout; the bootloader then enumerates as 1a86:55e0. */
[[noreturn]] void CH32RVUsbDevice::enterBootloader()
{
    if (Core::device && Core::device->_started) {
        Core::device->end();
    }
    const uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 50u) {
    }
    noInterrupts();
#ifdef CH32RV_USB_FACTORY_ISP_ENTRY
    port_set_boot_mode();
#endif
    CH32RV_PFIC_CFGR = CH32RV_PFIC_KEY3 | CH32RV_PFIC_SYSRST;
    for (;;) {
    }
}

/* The CDC ports one device can hold (CH32RVUsbDeviceCdcSerial.cpp). */
uint8_t CH32RVUsbDevice::maxCdcPorts()
{
    return 4;
}

/* ------------------------------------------------------------ descriptors for the sketch */
/* One heap buffer for every descriptor built on request, grown as needed. */
static uint8_t *scratch;
static uint16_t scratch_size;

static uint8_t *scratch_for(uint16_t n)
{
    if (n > scratch_size) {
        uint8_t *p = (uint8_t *)realloc(scratch, n);
        if (!p) {
            return nullptr;
        }
        scratch = p;
        scratch_size = n;
    }
    return scratch;
}

static const uint8_t *write_to_scratch(uint16_t n, void (*write)(CH32RVUsbDescriptorWriter &w))
{
    uint8_t *p = n ? scratch_for(n) : nullptr;
    if (!p) {
        return nullptr;
    }
    CH32RVUsbDescriptorWriter w;
    w.out = p;
    w.room = n;
    write(w);
    return p;
}

const uint8_t *CH32RVUsbDevice::deviceDescriptor()
{
    if (!_started || Core::device != this) {
        return nullptr;
    }
    build_device_descriptor(_config);                   /* the speed in use decides bcdUSB / EP0 */
    return desc_device;
}

const uint8_t *CH32RVUsbDevice::configurationDescriptorFor(CH32RVUsbSpeed speed)
{
    if (!_started || Core::device != this || !ch32rv_usb_dcd_can(*Core::dcd, speed)) {
        return nullptr;
    }
    CH32RVUsbDescriptorWriter count;
    const uint16_t n = buildConfiguration(count, speed, 2);
    uint8_t *p = n ? scratch_for(n) : nullptr;
    if (!p) {
        return nullptr;
    }
    CH32RVUsbDescriptorWriter w;
    w.out = p;
    w.room = n;
    buildConfiguration(w, speed, 2);
    return p;
}

const uint8_t *CH32RVUsbDevice::configurationDescriptor(uint8_t index)
{
    return index == 0 ? configurationDescriptorFor(_started ? Core::speed : CH32RVUsbSpeed::Full) : nullptr;
}

const uint8_t *CH32RVUsbDevice::configurationDescriptorForSpeed(uint8_t index, bool highSpeed)
{
    return index == 0 ? configurationDescriptorFor(highSpeed ? CH32RVUsbSpeed::High : CH32RVUsbSpeed::Full)
                      : nullptr;
}

const uint8_t *CH32RVUsbDevice::deviceQualifierDescriptor()
{
    if (!_started || Core::device != this || !ch32rv_usb_dcd_can(*Core::dcd, CH32RVUsbSpeed::High)) {
        return nullptr;
    }
    static uint8_t q[10];
    build_device_descriptor(_config);
    memcpy(q, desc_device, 8);
    q[0] = 10;
    q[1] = 6;
    q[8] = 1;
    q[9] = 0;
    return q;
}

const uint8_t *CH32RVUsbDevice::otherSpeedConfigurationDescriptor(uint8_t index, bool currentHighSpeed)
{
    if (index != 0 || !_started || Core::device != this || !ch32rv_usb_dcd_can(*Core::dcd, CH32RVUsbSpeed::High)) {
        return nullptr;
    }
    const CH32RVUsbSpeed other = currentHighSpeed ? CH32RVUsbSpeed::Full : CH32RVUsbSpeed::High;
    CH32RVUsbDescriptorWriter count;
    const uint16_t n = buildConfiguration(count, other, 7);
    uint8_t *p = n ? scratch_for(n) : nullptr;
    if (!p) {
        return nullptr;
    }
    CH32RVUsbDescriptorWriter w;
    w.out = p;
    w.room = n;
    buildConfiguration(w, other, 7);
    return p;
}

uint16_t CH32RVUsbDevice::bosDescriptorLength()
{
    if (!_started || Core::device != this) {
        return 0;
    }
    return ch32rv_usb_platform.bosLength ? ch32rv_usb_platform.bosLength : bos_length();
}

const uint8_t *CH32RVUsbDevice::bosDescriptor()
{
    const uint16_t n = bosDescriptorLength();
    if (!n) {
        return nullptr;
    }
    if (ch32rv_usb_platform.bosLength) {
        return write_to_scratch(n, ch32rv_usb_platform.bosWrite);
    }
    return ss_bos;
}

uint16_t CH32RVUsbDevice::microsoftOs20DescriptorLength()
{
    return _started && Core::device == this ? ch32rv_usb_platform.msOs20Length : 0;
}

const uint8_t *CH32RVUsbDevice::microsoftOs20Descriptor()
{
    const uint16_t n = microsoftOs20DescriptorLength();
    return n ? write_to_scratch(n, ch32rv_usb_platform.msOs20Write) : nullptr;
}

const uint16_t *CH32RVUsbDevice::stringDescriptor(uint8_t index, uint16_t langid)
{
    (void)langid;
    if (!_started || Core::device != this) {
        return nullptr;
    }
    return string_descriptor(index) ? reinterpret_cast<const uint16_t *>(desc_string) : nullptr;
}

uint16_t CH32RVUsbDevice::hidReportDescriptorLength(uint8_t instance)
{
    return instance == 0 && ch32rv_usb_hid_report ? ch32rv_usb_hid_report(nullptr) : 0;
}

const uint8_t *CH32RVUsbDevice::hidReportDescriptor(uint8_t instance)
{
    const uint16_t n = hidReportDescriptorLength(instance);
    uint8_t *p = n ? scratch_for(n) : nullptr;
    if (!p) {
        return nullptr;
    }
    CH32RVUsbDescriptorWriter w;
    w.out = p;
    w.room = n;
    ch32rv_usb_hid_report(&w);
    return p;
}

uint16_t (*ch32rv_usb_hid_report)(CH32RVUsbDescriptorWriter *w);

void CH32RVUsbDevice::onBusEvent(BusEventCallback callback)
{
    Core::bus_event = callback;
    Core::bus_record = Core::bus;
    Core::bus_deliver = Core::deliver_bus;
}

uint32_t CH32RVUsbDevice::droppedBusEvents() const
{
    return Core::bus_dropped;
}
