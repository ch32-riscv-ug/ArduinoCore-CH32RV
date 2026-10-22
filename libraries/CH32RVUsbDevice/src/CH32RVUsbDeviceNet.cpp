/* USB network adapter on the device stack: CDC-NCM with 16-bit transfer
 * blocks (NTB16). One block each way at a time; a block from the host may
 * carry several frames, the device sends one frame per block. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#define NET_MTU 1514
/* The smallest transfer block NCM 1.0 lets a function declare (3.2.1). */
#define NTB_MAX 2048
#define NOTIFY_EP_SIZE 16
#define NTH16_SIGNATURE 0x484D434Eu   /* "NCMH" */
#define NDP16_SIGNATURE 0x304D434Eu   /* "NCM0", no CRC */

static struct {
    uint8_t ep_notify, ep_out, ep_in, itf;
    uint16_t packet;
    uint16_t sequence;
    uint32_t in_max;          /* SET_NTB_INPUT_SIZE */
    bool up, in_busy, zlp, notify_busy;
    bool out_paused;          /* the receive gate said no: OUT not re-armed, the host waits */
    uint8_t notify_step;      /* speed, then connection */
} drv;

static uint8_t mac[6];
static bool mac_set;
static char mac_text[13];
static CH32RVUsbDeviceNet *net;
static uint8_t ntb_out[NTB_MAX] __attribute__((aligned(4)));
static uint8_t ntb_in[NTB_MAX] __attribute__((aligned(4)));
static uint8_t notify_buf[NOTIFY_EP_SIZE] __attribute__((aligned(4)));
static uint8_t ctl[28] __attribute__((aligned(4)));

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

static const char *mac_string(uint8_t index)
{
    return index == CH32RV_USB_EXTRA_STRING ? mac_text : nullptr;
}

/* Two interfaces (control, data); notification IN on endpoint n, bulk OUT
 * and IN on n + 1. The data interface has no endpoints until the host picks
 * alternate setting 1, which is when the link is up. */
static bool net_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf, uint8_t *ep,
                               uint8_t str, CH32RVUsbSpeed speed)
{
    (void)self;
    if (!mac_set) {
        /* Locally administered, unicast, from the 64-bit unique ID. */
        const uint32_t lo = *(volatile const uint32_t *)CH32RV_ESIG_UNIID1_ADDR;
        const uint32_t hi = *(volatile const uint32_t *)CH32RV_ESIG_UNIID2_ADDR;
        const uint32_t mix = lo ^ (hi * 2654435761u);
        mac[0] = 0x02;
        mac[1] = (uint8_t)(hi >> 8);
        mac[2] = (uint8_t)(mix >> 24);
        mac[3] = (uint8_t)(mix >> 16);
        mac[4] = (uint8_t)(mix >> 8);
        mac[5] = (uint8_t)mix;
    }
    static const char hex[] = "0123456789ABCDEF";
    for (uint8_t i = 0; i < 6; i++) {
        mac_text[2 * i] = hex[mac[i] >> 4];
        mac_text[2 * i + 1] = hex[mac[i] & 15];
    }
    mac_text[12] = 0;
    const uint8_t n = *ep;
    *ep = (uint8_t)(n + 2);
    drv.itf = itf;
    drv.ep_notify = (uint8_t)(0x80 | n);
    drv.ep_out = (uint8_t)(n + 1);
    drv.ep_in = (uint8_t)(0x80 | (n + 1));
    const uint16_t mps = ch32rv_usb_bulk_size(speed);
    const uint8_t d[] = {
        8, 11, itf, 2, 2, 0x0D, 0, str,                             /* association: the function's name too */
        9, 4, itf, 0, 1, 2, 0x0D, 0, str,                           /* communication interface, NCM */
        5, 0x24, 0x00, 0x10, 0x01,                                  /* header, CDC 1.10 */
        5, 0x24, 0x06, itf, (uint8_t)(itf + 1),                     /* union */
        13, 0x24, 0x0F, CH32RV_USB_EXTRA_STRING, 0, 0, 0, 0,        /* Ethernet: iMACAddress, no statistics */
        (uint8_t)NET_MTU, (uint8_t)(NET_MTU >> 8), 0, 0, 0,
        6, 0x24, 0x1A, 0x00, 0x01, 0,                               /* NCM 1.0, no optional requests */
        7, 5, (uint8_t)(0x80 | n), 3, NOTIFY_EP_SIZE, 0, ch32rv_usb_interval(speed, 16),
        9, 4, (uint8_t)(itf + 1), 0, 0, 0x0A, 0, 1, 0,              /* data, alternate 0: no link */
        9, 4, (uint8_t)(itf + 1), 1, 2, 0x0A, 0, 1, 0,              /* data, alternate 1 */
        7, 5, (uint8_t)(n + 1), 2, (uint8_t)mps, (uint8_t)(mps >> 8), 0,
        7, 5, (uint8_t)(0x80 | (n + 1)), 2, (uint8_t)mps, (uint8_t)(mps >> 8), 0,
    };
    w.put(d, sizeof d);
    return true;
}

static const CH32RVUsbFunctionType net_type = {net_descriptor, 2, 1, nullptr, 0};

CH32RVUsbDeviceNet::CH32RVUsbDeviceNet(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, net_type, CH32RV_USB_NET_NAME)
{
    if (attached()) {
        net = this;
        ch32rv_usb_extra_string = mac_string;
    }
}

void CH32RVUsbDeviceNet::macAddress(const uint8_t value[6])
{
    memcpy(mac, value, 6);
    mac_set = true;
}

const uint8_t *CH32RVUsbDeviceNet::macAddress() const
{
    return mac;
}

bool CH32RVUsbDeviceNet::linkUp() const
{
    return net == this && drv.up && device().mounted();
}

/* ---------------------------------------------------------- the stack's side */
void CH32RVUsbDeviceNet::usbReset()
{
    drv.up = drv.in_busy = drv.zlp = drv.notify_busy = drv.out_paused = false;
    drv.notify_step = 0;
    _held = 0;
}

void CH32RVUsbDeviceNet::usbConfigured(CH32RVUsbSpeed speed)
{
    drv.packet = ch32rv_usb_bulk_size(speed);
    drv.in_max = NTB_MAX;
    drv.sequence = 0;
    usbReset();
}

/* The link state after the host picks the data interface's setting: the
 * speed first, then "connected" (Linux takes the carrier from the second). */
void CH32RVUsbDeviceNet::notify()
{
    if (drv.notify_busy || drv.notify_step == 0) {
        return;
    }
    memset(notify_buf, 0, sizeof notify_buf);
    notify_buf[0] = 0xA1;
    put16(notify_buf + 4, drv.itf);
    uint16_t n = 8;
    if (drv.notify_step == 2) {
        notify_buf[1] = 0x2A;                    /* CONNECTION_SPEED_CHANGE */
        put16(notify_buf + 6, 8);
        const uint32_t bps = drv.packet >= 512 ? 480000000u : 12000000u;
        put32(notify_buf + 8, bps);
        put32(notify_buf + 12, bps);
        n = 16;
    } else {
        notify_buf[1] = 0x00;                    /* NETWORK_CONNECTION */
        put16(notify_buf + 2, drv.up ? 1 : 0);
    }
    drv.notify_busy = device().transfer(drv.ep_notify, notify_buf, n);
}

bool CH32RVUsbDeviceNet::usbSetInterface(uint8_t interfaceNumber, uint8_t alternate)
{
    if (interfaceNumber != (uint8_t)(drv.itf + 1) || alternate > 1) {
        return false;
    }
    drv.up = alternate == 1;
    drv.in_busy = drv.zlp = drv.out_paused = false;
    _held = 0;
    if (drv.up) {
        device().transfer(drv.ep_out, ntb_out, sizeof ntb_out);
    }
    drv.notify_busy = device().transferBusy(drv.ep_notify);
    drv.notify_step = drv.up ? 2 : 1;
    notify();
    return true;
}

bool CH32RVUsbDeviceNet::usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &s)
{
    if ((s.bmRequestType & 0x60) != 0x20) {
        return false;
    }
    const bool setup = stage == CH32RVUsbControlStage::Setup;
    switch (s.bRequest) {
    case 0x80:                                   /* GET_NTB_PARAMETERS */
        if (setup) {
            memset(ctl, 0, sizeof ctl);
            put16(ctl, sizeof ctl);
            put16(ctl + 2, 1);                   /* NTB16 only */
            put32(ctl + 4, NTB_MAX);
            put16(ctl + 8, 4);                   /* datagrams in on 4-byte boundaries */
            put16(ctl + 12, 4);
            put32(ctl + 16, NTB_MAX);
            put16(ctl + 20, 4);
            put16(ctl + 24, 4);
            return device().controlIn(ctl, sizeof ctl);
        }
        return true;
    case 0x83:                                   /* GET_NTB_FORMAT: NTB16 */
        if (setup) {
            put16(ctl, 0);
            return device().controlIn(ctl, 2);
        }
        return true;
    case 0x84:                                   /* SET_NTB_FORMAT */
        return !setup || (s.wValue == 0 && device().controlStatus());
    case 0x85:                                   /* GET_NTB_INPUT_SIZE */
        if (setup) {
            put32(ctl, drv.in_max);
            return device().controlIn(ctl, 4);
        }
        return true;
    case 0x86:                                   /* SET_NTB_INPUT_SIZE */
        if (setup) {
            return s.wLength >= 4 && s.wLength <= sizeof ctl && device().controlOut(ctl, s.wLength);
        }
        if (stage == CH32RVUsbControlStage::Data) {
            const uint32_t v = get32(ctl);
            drv.in_max = v < NTB_MAX ? v : NTB_MAX;
        }
        return true;
    case 0x43:                                   /* SET_ETHERNET_PACKET_FILTER: everything goes up */
        return !setup || device().controlStatus();
    }
    return false;
}

/* Each frame in a block from the host, in its NDP16 order, to `to`.
 * Returns how many. */
size_t CH32RVUsbDeviceNet::receiveBlock(uint16_t length, const FrameCallback &to)
{
    size_t frames = 0;
    if (length < 12 || get32(ntb_out) != NTH16_SIGNATURE || get16(ntb_out + 4) != 12) {
        return frames;
    }
    const uint16_t block = get16(ntb_out + 8) < length ? get16(ntb_out + 8) : length;
    uint16_t ndp = get16(ntb_out + 10);
    for (uint8_t guard = 0; ndp && guard < 8; guard++) {
        if ((uint32_t)ndp + 16 > block || get32(ntb_out + ndp) != NDP16_SIGNATURE) {
            return frames;
        }
        const uint16_t end = (uint16_t)(ndp + get16(ntb_out + ndp + 4));
        for (uint16_t p = (uint16_t)(ndp + 8); p + 4 <= end && p + 4 <= block; p += 4) {
            const uint16_t index = get16(ntb_out + p), size = get16(ntb_out + p + 2);
            if (!index || !size) {
                break;
            }
            if ((uint32_t)index + size <= block && to) {
                to(ntb_out + index, size);
                frames++;
            }
        }
        ndp = get16(ntb_out + ndp + 6);
    }
    return frames;
}

/* Re-arm OUT for the next block, unless the receive gate says no. */
void CH32RVUsbDeviceNet::receiveNext()
{
    if (_receiveGate && !_receiveGate()) {
        drv.out_paused = true;               /* resumeReceive() re-arms */
    } else {
        device().transfer(drv.ep_out, ntb_out, sizeof ntb_out);
    }
}

size_t CH32RVUsbDeviceNet::usbDeliver()
{
    if (!_held) {
        return 0;
    }
    /* Still held during the calls: nothing re-arms the endpoint over it. */
    const size_t ran = receiveBlock(_held, _onFrame);
    _held = 0;
    if (drv.up && net == this) {
        receiveNext();
    }
    return ran;
}

void CH32RVUsbDeviceNet::usbTransferred(uint8_t address, uint16_t length)
{
    if (address == drv.ep_out) {
        if (drv.up && length && !_stackFrame && _onFrame) {
            _held = length;                      /* the sketch's: wait for usbDeliver() */
        } else if (drv.up) {
            receiveBlock(length, _stackFrame);
            receiveNext();
        }
    } else if (address == drv.ep_in) {
        drv.in_busy = false;
        if (drv.zlp) {
            drv.zlp = false;                     /* the block ended on a full packet */
            drv.in_busy = device().transfer(drv.ep_in, ntb_in, 0);
        }
    } else if (address == drv.ep_notify) {
        drv.notify_busy = false;
        if (drv.notify_step) {
            drv.notify_step--;
        }
        notify();
    }
}

/* ---------------------------------------------------------- the sketch's side */
bool CH32RVUsbDeviceNet::sendFrame(const uint8_t *data, size_t length, uint32_t timeoutMs)
{
    const uint16_t block = (uint16_t)(28 + length);   /* NTH16, one NDP16, the frame */
    if (!data || !length || length > NET_MTU) {
        return false;
    }
    const uint32_t t0 = millis();
    for (;;) {
        ch32rv_usb_task();
        if (!linkUp() || block > drv.in_max) {
            return false;
        }
        if (!drv.in_busy) {
            break;
        }
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return false;
        }
    }
    put32(ntb_in, NTH16_SIGNATURE);
    put16(ntb_in + 4, 12);
    put16(ntb_in + 6, drv.sequence++);
    put16(ntb_in + 8, block);
    put16(ntb_in + 10, 12);
    put32(ntb_in + 12, NDP16_SIGNATURE);
    put16(ntb_in + 16, 16);
    put16(ntb_in + 18, 0);
    put16(ntb_in + 20, 28);
    put16(ntb_in + 22, (uint16_t)length);
    put32(ntb_in + 24, 0);                       /* the terminating entry */
    memcpy(ntb_in + 28, data, length);
    drv.zlp = block < drv.in_max && block % drv.packet == 0;
    drv.in_busy = device().transfer(drv.ep_in, ntb_in, block);
    return drv.in_busy;
}

void CH32RVUsbDeviceNet::resumeReceive()
{
    if (drv.up && drv.out_paused && net == this) {
        drv.out_paused = false;
        device().transfer(drv.ep_out, ntb_out, sizeof ntb_out);
    }
}


