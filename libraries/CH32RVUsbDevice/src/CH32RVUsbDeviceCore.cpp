/* The device stack: EP0 and the standard requests, the descriptors, the
 * endpoints of the configuration and its alternate settings, transfers for the
 * classes, suspend / resume, and the reset into the factory USB bootloader.
 *
 * The controller driver (CH32RVUsbDcd.h) only records events in its
 * interrupt; everything here, and every class callback, runs in normal
 * context from ch32rv_usb_task() (loop() / delay()). One device runs at a
 * time. Nothing is sized at build time beyond the configuration descriptor's
 * buffer (CH32RV_USB_CONFIG_MAX). */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#include "ch32rv_registers.h"
#include "CH32RVUsbDevicePort.h"

/* The configuration descriptor is built here. 512 bytes hold a UAC1 headset
 * (191) + a UVC camera (158) + CDC (66) + HID (32) + DFU runtime (18) + the
 * header (9); beyond that begin() fails with DescriptorTooLarge. */
#ifndef CH32RV_USB_CONFIG_MAX
#define CH32RV_USB_CONFIG_MAX 512
#endif

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

typedef CH32RVUsbDeviceCore Core;

#define EV_RESET   0x01
#define EV_SETUP   0x02
#define EV_SUSPEND 0x04
#define EV_RESUME  0x08

static uint8_t desc_configuration[CH32RV_USB_CONFIG_MAX];
static uint8_t desc_device[18];
static uint8_t desc_string[2 + 2 * 32];
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

static const uint8_t *bos(uint16_t *length)
{
    if (ch32rv_usb_platform.bosLength) {
        *length = ch32rv_usb_platform.bosLength;
        return ch32rv_usb_platform.bos;
    }
    if (ch32rv_usb_dcd_can(*Core::dcd, CH32RVUsbSpeed::Super)) {
        *length = sizeof ss_bos;
        return ss_bos;
    }
    *length = 0;
    return nullptr;
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
    for (uint8_t i = 0; i < device->_count; i++) {
        CH32RVUsbDeviceFunction *f = device->_functions[i];
        if (itf >= f->_interface && itf < f->_interface + f->_type.interfaces) {
            return f;
        }
    }
    return nullptr;
}

/* Open (or close) the endpoints of one alternate setting of an interface, as
 * the configuration descriptor lists them. */
void Core::open_interface(uint8_t itf, uint8_t alternate, bool open)
{
    const uint16_t total = (uint16_t)(desc_configuration[2] | desc_configuration[3] << 8);
    bool mine = false;
    CH32RVUsbDeviceFunction *f = interface_owner(itf);
    for (uint16_t at = desc_configuration[0]; at + 1 < total && desc_configuration[at]; at += desc_configuration[at]) {
        const uint8_t *d = desc_configuration + at;
        if (d[1] == 4) {                                 /* INTERFACE */
            mine = d[2] == itf && d[3] == alternate;
        } else if (d[1] == 5 && mine) {                  /* ENDPOINT */
            const uint8_t address = d[2], ep = address & 0x0F;
            const uint16_t mps = (uint16_t)((d[4] | d[5] << 8) & 0x7FF);
            const uint16_t bit = (uint16_t)(1u << ep);
            if (open) {
                dcd->epOpen(address, (uint8_t)(d[3] & 3), mps);
                ep_owner[ep] = f;
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
    }
}

void Core::set_configuration(bool on)
{
    dcd->epCloseAll();
    busy_in = busy_out = halt_in = halt_out = 0;
    memset(ep_owner, 0, sizeof ep_owner);
    memset(alt, 0, sizeof alt);
    if (configured) {
        configured = false;
        for (uint8_t i = 0; i < device->_count; i++) {
            device->_functions[i]->usbReset();
        }
    }
    if (!on) {
        return;
    }
    device->buildConfiguration(desc_configuration, sizeof desc_configuration, speed, 2);
    const uint8_t interfaces = desc_configuration[4];
    for (uint8_t itf = 0; itf < interfaces && itf < MAX_ITF; itf++) {
        open_interface(itf, 0, true);
    }
    configured = true;
    for (uint8_t i = 0; i < device->_count; i++) {
        device->_functions[i]->usbConfigured(speed);
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
    ep0_mps = speed == CH32RVUsbSpeed::Super ? 512 : 64;
    build_device_descriptor(device->_config);
}

/* ------------------------------------------------------------ descriptors */
static void build_device_descriptor(const CH32RVUsbDeviceConfig &c)
{
    /* bcdUSB 3.20 at SuperSpeed (EP0 then 2^9 bytes); otherwise 2.10 when
     * there is a BOS - a host reads it only from a 2.1 device. IAD: CDC,
     * MIDI, audio and video are interfaces bound together. */
    uint16_t bos_length;
    bos(&bos_length);
    const bool ss = Core::speed == CH32RVUsbSpeed::Super;
    const uint16_t bcd = ss ? 0x0320 : bos_length ? 0x0210 : 0x0200;
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
 * a configured serial number is the chip's 64-bit unique ID in hex. */
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
        const uint64_t v = ((uint64_t)hi << 32) | lo;
        for (uint8_t n = 0; n < 16; n++) {
            uid[n] = hex[(v >> (60 - 4 * n)) & 0xF];
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
        if (other && (!ch32rv_usb_dcd_can(*dcd, CH32RVUsbSpeed::High) || speed == CH32RVUsbSpeed::Super)) {
            return false;
        }
        const CH32RVUsbSpeed as = !other ? speed : speed == CH32RVUsbSpeed::High ? CH32RVUsbSpeed::Full
                                                                                 : CH32RVUsbSpeed::High;
        const uint16_t n = device->buildConfiguration(desc_configuration, sizeof desc_configuration, as,
                                                      (uint8_t)(s.wValue >> 8));
        return n && device->controlIn(desc_configuration, n);
    }
    case 3: {                                            /* STRING */
        const uint16_t n = string_descriptor((uint8_t)s.wValue);
        return n && device->controlIn(desc_string, n);
    }
    case 6:                                              /* DEVICE_QUALIFIER: high-speed capable only */
        if (!ch32rv_usb_dcd_can(*dcd, CH32RVUsbSpeed::High) || speed == CH32RVUsbSpeed::Super) {
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
    case 15: {                                           /* BOS */
        uint16_t n;
        const uint8_t *p = bos(&n);
        return p && device->controlIn(p, n);
    }
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
            if (s.wValue == 1 && speed != CH32RVUsbSpeed::Super) {   /* DEVICE_REMOTE_WAKEUP */
                remote_wakeup = s.bRequest == 3;
                return device->controlStatus();
            }
            if (s.wValue >= 48 && s.wValue <= 50 && speed == CH32RVUsbSpeed::Super) {   /* U1, U2, LTM */
                const uint8_t bit = (uint8_t)(4u << (s.wValue - 48));
                ss_features = (uint8_t)(s.bRequest == 3 ? ss_features | bit : ss_features & ~bit);
                return device->controlStatus();
            }
            return false;
        case 0x30:                                       /* SET_SEL (SuperSpeed) */
            return speed == CH32RVUsbSpeed::Super && device->controlOut(sel, sizeof sel);
        case 0x31:                                       /* SET_ISOCH_DELAY (SuperSpeed) */
            return speed == CH32RVUsbSpeed::Super && device->controlStatus();
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
            if (s.wValue != 0 || speed != CH32RVUsbSpeed::Super) {
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
    if (!ok) {
        dcd->ep0Stall();
        ep0 = IDLE;
    }
}

void Core::ep0_done(bool in, uint16_t length)
{
    (void)length;
    if (in && ep0 == DATA_IN) {
        if (zlp) {                                       /* a full last packet shorter than asked */
            zlp = false;
            dcd->xfer(0x80, nullptr, 0);
            return;
        }
        ep0 = STATUS_OUT;
        dcd->xfer(0x00, nullptr, 0);
    } else if (!in && ep0 == DATA_OUT) {
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
    }
}

/* ------------------------------------------------------------ the task */
void Core::task()
{
    noInterrupts();
    const uint8_t e = events;
    const uint16_t din = done_in, dout = done_out;
    uint8_t s[8];
    memcpy(s, setup_raw, 8);
    events = 0;
    done_in = done_out = 0;
    interrupts();

    if (e & EV_RESET) {
        reset_state();
    }
    if (e & EV_SUSPEND) {
        suspended = true;
    }
    if (e & EV_RESUME) {
        suspended = false;
    }
    if (e & EV_SETUP) {
        memcpy(setup_raw, s, 8);
        handle_setup();
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
    if (Core::bootloader) {
        if (!leave_at) {
            leave_at = millis() | 1u;
        } else if ((uint32_t)(millis() - leave_at) >= 20u) {
            CH32RVUsbDevice::enterBootloader();
        }
    }
    busy = true;
    Core::task();
    busy = false;
}

/* ------------------------------------------------------------ the device's API */
bool CH32RVUsbDevice::begin(const CH32RVUsbDeviceConfig &config)
{
    if (_started) {
        return true;
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
        if (ch32rv_usb_dcd_can(*dcd, (CH32RVUsbSpeed)sp) &&
            !buildConfiguration(desc_configuration, sizeof desc_configuration, (CH32RVUsbSpeed)sp, 2)) {
            return false;
        }
    }
    /* The BOS and the Microsoft OS 2.0 set need the interface numbers. */
    if (ch32rv_usb_platform.build && !ch32rv_usb_platform.build(*this)) {
        _error = CH32RVUsbDeviceError::InvalidArgument;
        return false;
    }
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
    dcd->connect(true);
    return true;
}

void CH32RVUsbDevice::end()
{
    if (!_started || Core::device != this) {
        return;
    }
    Core::dcd->connect(false);
    if (Core::configured) {
        Core::set_configuration(false);
    }
    Core::dcd->close();
    _started = false;
    Core::device = nullptr;
}

bool CH32RVUsbDevice::mounted() const
{
    return _started && Core::device == this && Core::configured;
}

uint8_t CH32RVUsbDevice::interfaceCount() const
{
    return desc_configuration[4];
}

bool CH32RVUsbDevice::suspended() const
{
    return _started && Core::suspended;
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
    if (!mounted() || ep == 0 || ep >= MAX_EP || !Core::ep_owner[ep]) {
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

