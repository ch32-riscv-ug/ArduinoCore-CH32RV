/* USB network adapter on the device stack: CDC-NCM with 16-bit transfer
 * blocks (NTB16), in a ring of buffers each way (one by default). A block
 * from the host may carry several frames; frames sent while a block is on
 * the wire are gathered into the next. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#define NET_MTU 1514
/* The smallest transfer block NCM 1.0 lets a function declare (3.2.1): the
 * class's own buffers, one each way. */
#define NTB_MIN 2048
#define NET_BUFFERS_MAX 8
#define NET_DATAGRAMS_MAX 32
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

    /* The settings begin() runs with (configure()). */
    uint8_t *tx_pool, *rx_pool;
    uint16_t tx_stride, rx_stride, ntb_in, ntb_out, max_frame;
    uint8_t tx_count, rx_count, max_datagrams;

    /* Transmit: blocks from tx_head on, the first on the wire while in_busy;
     * the last still takes frames while tx_open. */
    uint8_t tx_head, tx_used;
    bool tx_open;
    uint16_t tx_end[NET_BUFFERS_MAX];        /* where its next frame goes */
    uint8_t tx_frames[NET_BUFFERS_MAX];

    /* Receive: blocks waiting for onFrame() from rx_head on, then the one
     * the endpoint fills while rx_armed. */
    uint8_t rx_head, rx_held;
    bool rx_armed;
    uint16_t rx_length[NET_BUFFERS_MAX];
} drv;

static uint8_t mac[6];
static bool mac_set;
static char mac_text[13];
static CH32RVUsbDeviceNet *net;
static uint8_t ntb_out[NTB_MIN] __attribute__((aligned(4)));
static uint8_t ntb_in[NTB_MIN] __attribute__((aligned(4)));
static uint8_t notify_buf[NOTIFY_EP_SIZE] __attribute__((aligned(4)));
static uint8_t ctl[28] __attribute__((aligned(4)));

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

static uint8_t *tx_block(uint8_t i) { return drv.tx_pool + (size_t)i * drv.tx_stride; }
static uint8_t *rx_block(uint8_t i) { return drv.rx_pool + (size_t)i * drv.rx_stride; }
/* The NDP16 sits right after the NTH16, with room for every frame and the
 * terminating entry; the frames follow it. */
static uint16_t ndp_room(uint8_t datagrams) { return (uint16_t)(8 + 4 * (datagrams + 1)); }
static uint16_t align4(uint32_t n) { return (uint16_t)((n + 3u) & ~3u); }

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
        (uint8_t)drv.max_frame, (uint8_t)(drv.max_frame >> 8), 0, 0, 0,
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
        configure(_config);
    }
}

/* One direction's ring: the sketch's array, or the class's own block. */
static bool net_pool(uint8_t *buffer, size_t bytes, size_t count, uint16_t stride, uint8_t *own, uint8_t *&pool)
{
    if (!buffer) {
        if (count * stride > NTB_MIN) {
            return false;
        }
        pool = own;
        return true;
    }
    if (((uintptr_t)buffer & 3u) || bytes < count * stride) {
        return false;
    }
    pool = buffer;
    return true;
}

bool CH32RVUsbDeviceNet::configure(const CH32RVUsbNetConfig &config)
{
    const size_t in = config.ntbInBytes ? config.ntbInBytes : NTB_MIN;
    const size_t out = config.ntbOutBytes ? config.ntbOutBytes : NTB_MIN;
    const size_t txs = config.txBuffers ? config.txBuffers : 1;
    const size_t rxs = config.rxBuffers ? config.rxBuffers : 1;
    const size_t datagrams = config.maxDatagrams ? config.maxDatagrams : 8;
    const size_t frame = config.maxFrameBytes ? config.maxFrameBytes : NET_MTU;
    /* Receive arms whole packets of the fastest speed: a block of exactly
     * ntbOutBytes then ends by its length, not in the next buffer. */
    const size_t rx_stride = (out + CH32RV_USB_MAX_PACKET - 1) / CH32RV_USB_MAX_PACKET * CH32RV_USB_MAX_PACKET;
    if (net != this || device().started() || in < NTB_MIN || in > 65535u || out < NTB_MIN || rx_stride > 65535u ||
        txs > NET_BUFFERS_MAX || rxs > NET_BUFFERS_MAX || datagrams > NET_DATAGRAMS_MAX || frame < 14 ||
        12u + ndp_room((uint8_t)datagrams) + frame > in || 28u + frame > out) {
        return false;
    }
    uint8_t *tx, *rx;
    if (!net_pool(config.txBuffer, config.txBufferBytes, txs, align4(in), ntb_in, tx) ||
        !net_pool(config.rxBuffer, config.rxBufferBytes, rxs, (uint16_t)rx_stride, ntb_out, rx)) {
        return false;
    }
    if (config.txBuffer && config.rxBuffer && tx < rx + rxs * rx_stride && rx < tx + txs * align4(in)) {
        return false;                            /* the two rings overlap */
    }
    _config = config;
    drv.tx_pool = tx;
    drv.rx_pool = rx;
    drv.tx_stride = align4(in);
    drv.rx_stride = (uint16_t)rx_stride;
    drv.ntb_in = (uint16_t)in;
    drv.ntb_out = (uint16_t)out;
    drv.tx_count = (uint8_t)txs;
    drv.rx_count = (uint8_t)rxs;
    drv.max_datagrams = (uint8_t)datagrams;
    drv.max_frame = (uint16_t)frame;
    if (config.name) {
        setName(config.name);
    }
    return true;
}

size_t CH32RVUsbDeviceNet::ntbInCapacity() const { return device().started() ? drv.ntb_in : 0; }
size_t CH32RVUsbDeviceNet::ntbOutCapacity() const { return device().started() ? drv.ntb_out : 0; }
size_t CH32RVUsbDeviceNet::txBufferCount() const { return device().started() ? drv.tx_count : 0; }
size_t CH32RVUsbDeviceNet::rxBufferCount() const { return device().started() ? drv.rx_count : 0; }

bool CH32RVUsbDeviceNet::macAddress(const uint8_t value[6])
{
    if (device().started() || !value) {
        return false;                            /* the host has read it */
    }
    memcpy(mac, value, 6);
    mac_set = true;
    return true;
}

const uint8_t *CH32RVUsbDeviceNet::macAddress() const
{
    return mac;
}

bool CH32RVUsbDeviceNet::linkUp() const
{
    return net == this && drv.up && device().ready();
}

/* ---------------------------------------------------------- the stack's side */
/* Frames queued and blocks received are dropped: none carries over to the
 * next link. */
static void net_clear()
{
    drv.in_busy = drv.zlp = drv.out_paused = false;
    drv.tx_head = drv.tx_used = 0;
    drv.tx_open = false;
    drv.rx_head = drv.rx_held = 0;
    drv.rx_armed = false;
}

void CH32RVUsbDeviceNet::usbReset()
{
    drv.up = drv.notify_busy = false;
    net_clear();
    drv.notify_step = 0;
}

void CH32RVUsbDeviceNet::usbConfigured(CH32RVUsbSpeed speed)
{
    drv.packet = ch32rv_usb_bulk_size(speed);
    drv.in_max = drv.ntb_in;
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
    net_clear();
    if (drv.up) {
        receiveNext();
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
            put32(ctl + 4, drv.ntb_in);
            put16(ctl + 8, 4);                   /* datagrams in on 4-byte boundaries */
            put16(ctl + 12, 4);
            put32(ctl + 16, drv.ntb_out);
            put16(ctl + 20, 4);
            put16(ctl + 24, 4);
            put16(ctl + 26, drv.max_datagrams);  /* wNtbOutMaxDatagrams */
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
            drv.in_max = v < drv.ntb_in ? v : drv.ntb_in;
        }
        return true;
    case 0x43:                                   /* SET_ETHERNET_PACKET_FILTER: everything goes up */
        return !setup || device().controlStatus();
    }
    return false;
}

/* Each frame in a block from the host, in its NDP16 order, to `to`.
 * Returns how many. */
size_t CH32RVUsbDeviceNet::receiveBlock(const uint8_t *ntb, uint16_t length, const FrameCallback &to)
{
    size_t frames = 0;
    if (length < 12 || get32(ntb) != NTH16_SIGNATURE || get16(ntb + 4) != 12) {
        _stats.invalidNtbs++;
        return frames;
    }
    const uint16_t block = get16(ntb + 8) < length ? get16(ntb + 8) : length;
    uint16_t ndp = get16(ntb + 10);
    for (uint8_t guard = 0; ndp && guard < 8; guard++) {
        if ((uint32_t)ndp + 16 > block || get32(ntb + ndp) != NDP16_SIGNATURE) {
            _stats.invalidNtbs++;
            return frames;
        }
        const uint16_t end = (uint16_t)(ndp + get16(ntb + ndp + 4));
        for (uint16_t p = (uint16_t)(ndp + 8); p + 4 <= end && p + 4 <= block; p += 4) {
            const uint16_t index = get16(ntb + p), size = get16(ntb + p + 2);
            if (!index || !size) {
                break;
            }
            if ((uint32_t)index + size <= block && to) {
                to(ntb + index, size);
                frames++;
                _stats.rxFrames++;
            }
        }
        ndp = get16(ntb + ndp + 6);
    }
    return frames;
}

/* Arm OUT on the next free buffer, unless one is armed, every buffer holds
 * a block for onFrame(), or the receive gate says no. */
void CH32RVUsbDeviceNet::receiveNext()
{
    if (!drv.up || net != this || drv.rx_armed || drv.out_paused || drv.rx_held >= drv.rx_count) {
        return;
    }
    if (_receiveGate && !_receiveGate()) {
        drv.out_paused = true;               /* resumeReceive() re-arms */
        return;
    }
    const uint8_t i = (uint8_t)((drv.rx_head + drv.rx_held) % drv.rx_count);
    drv.rx_armed = device().transfer(drv.ep_out, rx_block(i), drv.rx_stride);
}

size_t CH32RVUsbDeviceNet::usbDeliver()
{
    size_t ran = 0;
    /* Each block is still held during its calls: nothing re-arms over it. */
    while (drv.rx_held) {
        ran += receiveBlock(rx_block(drv.rx_head), drv.rx_length[drv.rx_head], _onFrame);
        drv.rx_head = (uint8_t)((drv.rx_head + 1) % drv.rx_count);
        drv.rx_held--;
        receiveNext();
    }
    return ran;
}

/* The oldest block goes on the wire when the endpoint is free; it takes no
 * more frames from then on. */
void CH32RVUsbDeviceNet::sendNext()
{
    if (drv.in_busy || !drv.tx_used) {
        return;
    }
    const uint8_t i = drv.tx_head;
    if (drv.tx_used == 1) {
        drv.tx_open = false;
    }
    uint8_t *b = tx_block(i);
    const uint8_t n = drv.tx_frames[i];
    const uint16_t block = drv.tx_end[i];
    put16(b + 6, drv.sequence++);
    put16(b + 8, block);
    put16(b + 16, (uint16_t)(8 + 4 * (n + 1)));
    put32(b + 20 + 4 * n, 0);                    /* the terminating entry */
    drv.zlp = block < drv.in_max && block % drv.packet == 0;
    drv.in_busy = device().transfer(drv.ep_in, b, block);
    if (!drv.in_busy) {
        drv.tx_head = (uint8_t)((drv.tx_head + 1) % drv.tx_count);   /* dropped */
        drv.tx_used--;
    }
}

void CH32RVUsbDeviceNet::usbTransferred(uint8_t address, uint16_t length)
{
    if (address == drv.ep_out) {
        drv.rx_armed = false;
        if (!drv.up) {
            return;
        }
        const uint8_t i = (uint8_t)((drv.rx_head + drv.rx_held) % drv.rx_count);
        if (length && !_stackFrame && _onFrame) {
            drv.rx_length[i] = length;           /* the sketch's: wait for usbDeliver() */
            drv.rx_held++;
        } else {
            receiveBlock(rx_block(i), length, _stackFrame);
        }
        receiveNext();
    } else if (address == drv.ep_in) {
        drv.in_busy = false;
        if (drv.zlp) {
            drv.zlp = false;                     /* the block ended on a full packet */
            drv.in_busy = device().transfer(drv.ep_in, tx_block(drv.tx_head), 0);
            return;
        }
        if (drv.tx_used) {
            _stats.txFrames += drv.tx_frames[drv.tx_head];
            drv.tx_head = (uint8_t)((drv.tx_head + 1) % drv.tx_count);
            drv.tx_used--;
        }
        sendNext();
    } else if (address == drv.ep_notify) {
        drv.notify_busy = false;
        if (drv.notify_step) {
            drv.notify_step--;
        }
        notify();
    }
}

/* ---------------------------------------------------------- the sketch's side */
/* Into the open block when the frame fits there, else a new block. */
static bool net_queue(const uint8_t *data, uint16_t length)
{
    const uint16_t limit = drv.in_max < drv.ntb_in ? (uint16_t)drv.in_max : drv.ntb_in;
    uint8_t i = (uint8_t)((drv.tx_head + drv.tx_used + drv.tx_count - 1) % drv.tx_count);
    if (!drv.tx_open || drv.tx_frames[i] >= drv.max_datagrams || align4(drv.tx_end[i]) + length > limit) {
        if (drv.tx_used >= drv.tx_count) {
            return false;
        }
        i = (uint8_t)((drv.tx_head + drv.tx_used) % drv.tx_count);
        drv.tx_used++;
        drv.tx_open = true;
        uint8_t *b = tx_block(i);
        put32(b, NTH16_SIGNATURE);
        put16(b + 4, 12);
        put16(b + 10, 12);                       /* the NDP16 */
        put32(b + 12, NDP16_SIGNATURE);
        put16(b + 18, 0);
        drv.tx_frames[i] = 0;
        drv.tx_end[i] = (uint16_t)(12 + ndp_room(drv.max_datagrams));
    }
    uint8_t *b = tx_block(i);
    const uint16_t at = align4(drv.tx_end[i]);
    const uint8_t n = drv.tx_frames[i]++;
    put16(b + 20 + 4 * n, at);
    put16(b + 22 + 4 * n, length);
    memcpy(b + at, data, length);
    drv.tx_end[i] = (uint16_t)(at + length);
    return true;
}

bool CH32RVUsbDeviceNet::sendFrame(const uint8_t *data, size_t length, uint32_t timeoutMs)
{
    if (!data || !length || length > drv.max_frame) {
        return false;
    }
    const uint32_t t0 = millis();
    for (;;) {
        ch32rv_usb_task();
        if (!linkUp() || 12u + ndp_room(drv.max_datagrams) + length > drv.in_max) {
            return false;
        }
        if (net_queue(data, (uint16_t)length)) {
            break;
        }
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return false;
        }
    }
    sendNext();
    return true;
}

void CH32RVUsbDeviceNet::resumeReceive()
{
    if (drv.out_paused && net == this) {
        drv.out_paused = false;
        receiveNext();
    }
}
