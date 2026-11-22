/* CH32RVUsbHost: a USB network function - CDC-NCM (NCM 1.0, 16-bit blocks)
 * and CDC-ECM - as raw Ethernet frames, in EspUsbHost's calls. One open at a
 * time.
 *
 * networkOpen() finds the function in the active configuration: the
 * communication interface (class 0x02, subclass 0x0D NCM or 0x06 ECM) with its
 * Ethernet Networking descriptor (the MAC address string, the largest
 * segment), NCM's functional descriptor and Union (the data interface), its
 * notification endpoint, and the data interface's alternate setting 1 with the
 * two bulk endpoints. NCM's GET_NTB_PARAMETERS gives the block sizes and the
 * datagram placement; a device that offers bigger receive blocks than this
 * host takes is asked for smaller ones (SET_NTB_INPUT_SIZE). Then the packet
 * filter (directed, broadcast, all multicast) and SET_INTERFACE to alternate
 * 1, which starts the data.
 *
 * Receiving: the bulk IN transfer is kept armed into one block's buffer; the
 * frames taken out of each block go to a ring (2-byte length, frame) that
 * onNetworkFrame() or networkReadFrame() empties. A ring without room for a
 * whole block holds the device off (the IN is not armed). Sending: one frame
 * per block (NCM: NTH16, NDP16, the datagram where the device's divisor and
 * remainder put it), a synchronous bulk OUT; a block that ends on a packet
 * boundary is padded by a byte (NCM) or followed by a zero-length packet
 * (ECM), so the device sees its end. */
#include "internal/CH32RVUsbHostInternal.h"

#include <stdlib.h>
#include <string.h>

#if CH32RV_USB_HOST_STACK

namespace {

constexpr uint8_t CDC_SET_ETHERNET_PACKET_FILTER = 0x43;
constexpr uint8_t NCM_GET_NTB_PARAMETERS = 0x80;
constexpr uint8_t NCM_SET_NTB_INPUT_SIZE = 0x86;
constexpr uint32_t NTH16_SIGNATURE = 0x484D434Eu;   /* "NCMH" */
constexpr uint32_t NDP16_SIGNATURE = 0x304D434Eu;   /* "NCM0" */
constexpr uint32_t NDP16_CRC_SIGNATURE = 0x314D434Eu;   /* "NCM1": a CRC after each datagram, not counted in it */
/* What a sent block needs besides the frame: NTH16 (12), NDP16 with one
 * datagram and the terminator (16), the alignment and the divisor's gap. */
constexpr uint16_t TX_SLACK = 96;

struct Net {
    bool open;
    volatile bool rxBusy, rxReady, notifyBusy;
    volatile uint16_t rxLength;
    bool linkUp;
    CH32RVUsbHostNetworkInterfaceInfo info;
    uint16_t rxSize, txSize, ntbOutMax;
    uint16_t outDivisor, outRemainder, outAlign;
    uint16_t sequence;
    uint8_t *rx, *tx, *frame, *ring;
    uint16_t ringSize, head, tail, used;
    CH32RVUsbHostNetworkStats stats;
    uint8_t notify[16] __attribute__((aligned(4)));
    uint8_t ctl[32] __attribute__((aligned(4)));
};
Net net;

uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

/* The ring: each frame after its 2-byte length, wrapping byte by byte. */
void ring_byte(uint8_t b)
{
    net.ring[net.head] = b;
    net.head = (uint16_t)((net.head + 1u) % net.ringSize);
}
uint8_t ring_take()
{
    const uint8_t b = net.ring[net.tail];
    net.tail = (uint16_t)((net.tail + 1u) % net.ringSize);
    return b;
}
void ring_put(const uint8_t *data, uint16_t length)
{
    if (length < 14 || length > CH32RV_USB_HOST_NETWORK_MAX_FRAME) {
        net.stats.rxOversized++;
        return;
    }
    if ((uint32_t)net.ringSize - net.used < length + 2u) {
        net.stats.rxDropped++;
        return;
    }
    ring_byte((uint8_t)length);
    ring_byte((uint8_t)(length >> 8));
    for (uint16_t i = 0; i < length; i++) {
        ring_byte(data[i]);
    }
    net.used = (uint16_t)(net.used + length + 2u);
    net.stats.rxFrames++;
}
/* The next frame into `out` (cut to `capacity`); its whole length, 0 when none. */
uint16_t ring_get(uint8_t *out, size_t capacity)
{
    if (!net.used) {
        return 0;
    }
    uint16_t length = ring_take();
    length = (uint16_t)(length | ring_take() << 8);
    for (uint16_t i = 0; i < length; i++) {
        const uint8_t b = ring_take();
        if (i < capacity) {
            out[i] = b;
        }
    }
    net.used = (uint16_t)(net.used - length - 2u);
    return length;
}

/* An NTB16 block: each NDP16's datagrams into the ring. */
void take_ntb(const uint8_t *b, uint16_t length)
{
    if (length < 12 || le32(b) != NTH16_SIGNATURE || le16(b + 4) != 12) {
        net.stats.rxOversized++;
        return;
    }
    const uint16_t block = le16(b + 8);
    if (block > length || block > net.rxSize) {
        net.stats.rxOversized++;
        return;
    }
    uint16_t ndp = le16(b + 10);
    for (uint8_t hops = 0; ndp && hops < 8; hops++) {
        if ((uint32_t)ndp + 16u > block) {
            net.stats.rxOversized++;
            return;
        }
        const uint32_t sig = le32(b + ndp);
        const uint16_t ndpLength = le16(b + ndp + 4);
        if ((sig != NDP16_SIGNATURE && sig != NDP16_CRC_SIGNATURE) || ndpLength < 16 ||
            (uint32_t)ndp + ndpLength > block) {
            net.stats.rxOversized++;
            return;
        }
        for (uint16_t at = 8; at + 4u <= ndpLength; at = (uint16_t)(at + 4u)) {
            const uint16_t index = le16(b + ndp + at), dlen = le16(b + ndp + at + 2);
            if (!index || !dlen) {
                break;
            }
            if ((uint32_t)index + dlen > block) {
                net.stats.rxOversized++;
                break;
            }
            ring_put(b + index, dlen);
        }
        ndp = le16(b + ndp + 6);
    }
}

void rx_done(void *user, uint8_t result, uint16_t length)
{
    (void)user;
    net.rxBusy = false;
    if (!net.open || result != CH32RV_USBH_OK) {
        return;
    }
    net.rxLength = length;
    net.rxReady = true;
}

void notify_done(void *user, uint8_t result, uint16_t length)
{
    (void)user;
    net.notifyBusy = false;
    if (!net.open || result != CH32RV_USBH_OK || length < 8 || net.notify[0] != 0xA1) {
        return;
    }
    if (net.notify[1] == 0x00) {                /* NETWORK_CONNECTION */
        net.linkUp = le16(net.notify + 2) != 0;
    }
}

void arm()
{
    if (!net.open || !ch32rv_usb_host_connected(net.info.address)) {
        return;
    }
    if (!net.rxBusy && !net.rxReady && (uint32_t)net.ringSize - net.used >= (uint32_t)net.rxSize + 2u) {
        net.rxBusy = true;
        if (!ch32rv_usb_host_xfer(net.info.address, net.info.inEndpoint, net.rx, net.rxSize, rx_done, nullptr)) {
            net.rxBusy = false;
        }
    }
    if (net.info.notificationEndpoint && !net.notifyBusy) {
        net.notifyBusy = true;
        const uint16_t n = ch32rv_min<uint16_t>(net.info.notificationMaxPacketSize, sizeof net.notify);
        if (!ch32rv_usb_host_xfer(net.info.address, net.info.notificationEndpoint, net.notify, n, notify_done,
                                  nullptr)) {
            net.notifyBusy = false;
        }
    }
}

void poll(CH32RVUsbHost &host)
{
    (void)host;
    if (!net.open) {
        return;
    }
    if (net.rxReady) {
        net.stats.rxNtb++;
        if (net.info.protocol == CH32RV_USB_HOST_NETWORK_PROTOCOL_CDC_NCM) {
            take_ntb(net.rx, net.rxLength);
        } else {
            ring_put(net.rx, net.rxLength);
        }
        net.rxReady = false;
    }
    arm();
}

size_t deliver(CH32RVUsbHost &host)
{
    size_t ran = 0;
    while (host._networkFrame && net.open && net.used) {
        const uint16_t n = ring_get(net.frame, CH32RV_USB_HOST_NETWORK_MAX_FRAME);
        CH32RVUsbHostNetworkFrame f;
        f.address = net.info.address;
        f.protocol = net.info.protocol;
        f.data = net.frame;
        f.length = ch32rv_min<uint16_t>(n, CH32RV_USB_HOST_NETWORK_MAX_FRAME);
        host._networkFrame(f);
        ran++;
    }
    return ran;
}

void release()
{
    free(net.rx);
    free(net.tx);
    free(net.frame);
    free(net.ring);
    memset(&net, 0, sizeof net);
}

void detach(uint8_t daddr)
{
    if (net.open && net.info.address == daddr) {
        release();                 /* the device and its endpoints are gone */
    }
}

CH32RVUsbHostGroup net_group = {nullptr, nullptr, poll, detach, deliver, nullptr};

struct Join {
    Join() { ch32rv_usb_host_join(net_group); }
} join;

bool matches(uint8_t address)
{
    return net.open && (address == CH32RV_USB_HOST_ANY_ADDRESS || net.info.address == address);
}

int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* The network function of `daddr`: its interfaces, descriptors and endpoint
 * descriptors (each with its SuperSpeed companion), into `info` / `keep`. */
bool find(uint8_t daddr, CH32RVUsbHostNetworkInterfaceInfo &info, uint8_t keep[3][13])
{
    info = CH32RVUsbHostNetworkInterfaceInfo();
    info.address = daddr;
    memset(keep, 0, 3 * 13);
    CH32RVUsbHostConfigWalk w;
    enum { OUTSIDE, CONTROL, DATA } where = OUTSIDE;
    for (const uint8_t *d = ch32rv_usb_host_config_first(w, daddr); d; d = ch32rv_usb_host_config_next(w)) {
        if (d[1] == 4 && d[0] >= 9) {
            if (info.protocol == CH32RV_USB_HOST_NETWORK_PROTOCOL_NONE) {
                if (d[5] == 0x02 && (d[6] == 0x0D || d[6] == 0x06) && d[3] == 0) {
                    info.protocol = d[6] == 0x0D ? CH32RV_USB_HOST_NETWORK_PROTOCOL_CDC_NCM
                                                 : CH32RV_USB_HOST_NETWORK_PROTOCOL_CDC_ECM;
                    info.controlInterfaceNumber = d[2];
                    info.dataInterfaceNumber = (uint8_t)(d[2] + 1u);   /* unless a Union says otherwise */
                    where = CONTROL;
                }
                continue;
            }
            if (d[5] == 0x0A && d[2] == info.dataInterfaceNumber) {
                where = (d[3] != 0 && d[4] >= 2) ? DATA : OUTSIDE;
                if (where == DATA) {
                    info.dataInterfaceAlternate = d[3];
                }
                continue;
            }
            if (info.inEndpoint && info.outEndpoint) {
                break;
            }
            where = OUTSIDE;
            continue;
        }
        if (where == CONTROL && d[1] == 0x24 && d[0] >= 3) {
            if (d[2] == 0x06 && d[0] >= 5) {                  /* Union */
                info.dataInterfaceNumber = d[4];
            } else if (d[2] == 0x0F && d[0] >= 13) {          /* Ethernet Networking */
                info.macAddressStringIndex = d[3];
                info.maxSegmentSize = le16(d + 8);
            } else if (d[2] == 0x1A && d[0] >= 6) {           /* NCM */
                info.ncmVersion = le16(d + 3);
                info.networkCapabilities = d[5];
            }
            continue;
        }
        if (d[1] != 5 || d[0] < 7 || where == OUTSIDE) {
            continue;
        }
        const uint8_t type = d[3] & 3;
        const bool in = d[2] & 0x80;
        const uint16_t mps = (uint16_t)((d[4] | d[5] << 8) & 0x7FF);
        int slot = -1;
        if (where == CONTROL && type == CH32RV_USBH_INTERRUPT && in && !info.notificationEndpoint) {
            info.notificationEndpoint = d[2];
            info.notificationMaxPacketSize = mps;
            slot = 0;
        } else if (where == DATA && type == CH32RV_USBH_BULK && in && !info.inEndpoint) {
            info.inEndpoint = d[2];
            info.inMaxPacketSize = mps;
            slot = 1;
        } else if (where == DATA && type == CH32RV_USBH_BULK && !in && !info.outEndpoint) {
            info.outEndpoint = d[2];
            info.outMaxPacketSize = mps;
            slot = 2;
        }
        if (slot >= 0) {
            const uint16_t room = ch32rv_usb_host_config_room(w);
            memcpy(keep[slot], d, 7);
            keep[slot][0] = 7;
            if (room >= d[0] + 6u && d[d[0]] >= 6 && d[d[0] + 1] == 0x30) {
                memcpy(keep[slot] + 7, d + d[0], 6);
            }
        }
    }
    return info.protocol != CH32RV_USB_HOST_NETWORK_PROTOCOL_NONE && info.inEndpoint && info.outEndpoint &&
           info.inMaxPacketSize && info.outMaxPacketSize;
}

}  // namespace

bool CH32RVUsbHost::networkOpen(uint8_t address)
{
    if (!_started) {
        return fail(CH32RVUsbHostError::NotStarted);
    }
    networkClose(CH32RV_USB_HOST_ANY_ADDRESS);
    CH32RVUsbHostNetworkInterfaceInfo info;
    uint8_t keep[3][13];
    uint8_t daddr = 0;
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES && !daddr; a++) {
        if ((address == CH32RV_USB_HOST_ANY_ADDRESS || a == address) && ch32rv_usb_host_device(a) && find(a, info, keep)) {
            daddr = a;
        }
    }
    if (!daddr) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    const uint8_t itf = info.controlInterfaceNumber;
    uint16_t rxSize, txSize, ntbOutMax = 0xFFFF;
    uint16_t divisor = 4, remainder = 0, align = 4;
    if (info.protocol == CH32RV_USB_HOST_NETWORK_PROTOCOL_CDC_NCM) {
        size_t got = 0;
        if (!ch32rv_usb_host_control(*this, daddr, 0xA1, NCM_GET_NTB_PARAMETERS, 0, itf, net.ctl, 28, &got, 1000) ||
            got < 28 || !(le16(net.ctl + 2) & 1)) {
            return fail(CH32RVUsbHostError::NotSupported);
        }
        const uint32_t inMax = le32(net.ctl + 4);
        const uint32_t outMax = le32(net.ctl + 16);
        divisor = le16(net.ctl + 20) ? le16(net.ctl + 20) : 1;
        remainder = (uint16_t)(le16(net.ctl + 22) % divisor);
        align = le16(net.ctl + 24) >= 4 ? le16(net.ctl + 24) : 4;
        rxSize = (uint16_t)(inMax < CH32RV_USB_HOST_NETWORK_NTB_IN_MAX ? inMax : CH32RV_USB_HOST_NETWORK_NTB_IN_MAX);
        if (inMax > rxSize) {
            put32(net.ctl, rxSize);
            if (!ch32rv_usb_host_control(*this, daddr, 0x21, NCM_SET_NTB_INPUT_SIZE, 0, itf, net.ctl, 4, nullptr,
                                         1000)) {
                return fail(CH32RVUsbHostError::NotSupported);   /* its blocks would not fit */
            }
        }
        ntbOutMax = (uint16_t)(outMax < 0xFFFFu ? outMax : 0xFFFFu);
        txSize = (uint16_t)(CH32RV_USB_HOST_NETWORK_MAX_FRAME + TX_SLACK + divisor);
        if (txSize > ntbOutMax) {
            txSize = ntbOutMax;
        }
    } else {
        /* ECM: a frame per transfer, ended by a short packet. */
        rxSize = (uint16_t)((CH32RV_USB_HOST_NETWORK_MAX_FRAME / info.inMaxPacketSize + 1u) * info.inMaxPacketSize);
        txSize = CH32RV_USB_HOST_NETWORK_MAX_FRAME;
    }
    uint8_t *rx = (uint8_t *)malloc(rxSize);
    uint8_t *tx = (uint8_t *)malloc(txSize);
    uint8_t *frame = (uint8_t *)malloc(CH32RV_USB_HOST_NETWORK_MAX_FRAME);
    uint8_t *ring = (uint8_t *)malloc(CH32RV_USB_HOST_NETWORK_RX_RING_SIZE);
    if (!rx || !tx || !frame || !ring) {
        free(rx);
        free(tx);
        free(frame);
        free(ring);
        return fail(CH32RVUsbHostError::NoMemory);
    }
    /* Directed, broadcast and all multicast (CDC ECM 6.2.4); a device
     * without the request takes what it takes. */
    ch32rv_usb_host_control(*this, daddr, 0x21, CDC_SET_ETHERNET_PACKET_FILTER, 0x000E, itf, nullptr, 0, nullptr,
                            1000);
    for (uint8_t i = 0; i < 3; i++) {
        if (keep[i][0] && !ch32rv_usb_host_ep_open(daddr, keep[i])) {
            free(rx);
            free(tx);
            free(frame);
            free(ring);
            return fail(CH32RVUsbHostError::TransferFailed);
        }
    }
    if (!ch32rv_usb_host_control(*this, daddr, 0x01, 0x0B, info.dataInterfaceAlternate, info.dataInterfaceNumber,
                                 nullptr, 0, nullptr, 1000)) {
        free(rx);
        free(tx);
        free(frame);
        free(ring);
        return fail(CH32RVUsbHostError::TransferFailed);
    }
    char mac[CH32RV_USB_HOST_STRING_MAX];
    if (info.macAddressStringIndex && getStringDescriptor(daddr, info.macAddressStringIndex, mac, sizeof mac) &&
        strlen(mac) >= 12) {
        for (uint8_t i = 0; i < 6; i++) {
            const int hi = hex_digit(mac[2 * i]), lo = hex_digit(mac[2 * i + 1]);
            info.macAddress[i] = (uint8_t)((hi < 0 ? 0 : hi) << 4 | (lo < 0 ? 0 : lo));
        }
    }
    memset(&net, 0, sizeof net);
    net.info = info;
    net.rx = rx;
    net.tx = tx;
    net.frame = frame;
    net.ring = ring;
    net.rxSize = rxSize;
    net.txSize = txSize;
    net.ntbOutMax = ntbOutMax;
    net.ringSize = CH32RV_USB_HOST_NETWORK_RX_RING_SIZE;
    net.outDivisor = divisor;
    net.outRemainder = remainder;
    net.outAlign = align;
    net.linkUp = !info.notificationEndpoint;
    net.stats.ntbInSize = rxSize;
    net.stats.ntbOutSize = txSize;
    net.open = true;
    ch32rv_usb_host_mark_claimed(daddr, info.controlInterfaceNumber);
    ch32rv_usb_host_mark_claimed(daddr, info.dataInterfaceNumber);
    arm();
    return ok();
}

void CH32RVUsbHost::networkClose(uint8_t address)
{
    if (!matches(address)) {
        return;
    }
    const uint8_t daddr = net.info.address;
    if (ch32rv_usb_host_connected(daddr)) {
        for (uint8_t ep : {net.info.inEndpoint, net.info.outEndpoint, net.info.notificationEndpoint}) {
            if (ep) {
                ch32rv_usb_host_abort(daddr, ep);
                ch32rv_usb_host_ep_close(daddr, ep);
            }
        }
        /* Alternate 0: the device stops the data (NCM 7.2). */
        ch32rv_usb_host_control_sync(daddr, 0x01, 0x0B, 0, net.info.dataInterfaceNumber, nullptr, 0, nullptr, 1000);
    }
    release();
}

bool CH32RVUsbHost::networkReady(uint8_t address) const
{
    return matches(address);
}

bool CH32RVUsbHost::getNetworkInterface(CH32RVUsbHostNetworkInterfaceInfo &info) const
{
    if (!net.open) {
        return false;
    }
    info = net.info;
    return true;
}

bool CH32RVUsbHost::networkWriteFrame(const uint8_t *frame, size_t length, uint8_t address)
{
    if (!matches(address)) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    if (!frame || length < 14 || length > CH32RV_USB_HOST_NETWORK_MAX_FRAME) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    uint16_t total;
    const uint16_t mps = net.info.outMaxPacketSize;
    bool zlp = false;
    if (net.info.protocol == CH32RV_USB_HOST_NETWORK_PROTOCOL_CDC_NCM) {
        const uint16_t ndp = (uint16_t)((12u + net.outAlign - 1u) / net.outAlign * net.outAlign);
        uint16_t at = (uint16_t)(ndp + 16u);
        at = (uint16_t)(at + (net.outRemainder + net.outDivisor - at % net.outDivisor) % net.outDivisor);
        total = (uint16_t)(at + length);
        if (total % mps == 0 && total < net.ntbOutMax) {
            total++;               /* the block ends on a short packet */
        }
        if (total > net.txSize) {
            net.stats.txFails++;
            return fail(CH32RVUsbHostError::InvalidArgument);
        }
        uint8_t *b = net.tx;
        memset(b, 0, at);
        put32(b, NTH16_SIGNATURE);
        put16(b + 4, 12);
        put16(b + 6, net.sequence++);
        put16(b + 8, total);
        put16(b + 10, ndp);
        put32(b + ndp, NDP16_SIGNATURE);
        put16(b + ndp + 4, 16);
        put16(b + ndp + 6, 0);
        put16(b + ndp + 8, at);
        put16(b + ndp + 10, (uint16_t)length);
        memcpy(b + at, frame, length);
        if (total > at + length) {
            b[total - 1] = 0;
        }
    } else {
        total = (uint16_t)length;
        memcpy(net.tx, frame, length);
        zlp = total % mps == 0;
    }
    uint16_t moved = 0;
    uint8_t r = ch32rv_usb_host_xfer_sync(net.info.address, net.info.outEndpoint, net.tx, total, &moved, 1000);
    if (r == CH32RV_USBH_OK && zlp) {
        r = ch32rv_usb_host_xfer_sync(net.info.address, net.info.outEndpoint, net.tx, 0, nullptr, 1000);
    }
    if (r != CH32RV_USBH_OK) {
        net.stats.txFails++;
        return fail(ch32rv_usb_host_result_error(r));
    }
    net.stats.txFrames++;
    return ok();
}

size_t CH32RVUsbHost::networkReadFrame(uint8_t *buffer, size_t length, uint8_t address)
{
    if (!matches(address) || !buffer) {
        return 0;
    }
    ch32rv_usb_host_task();
    const uint16_t n = ring_get(buffer, length);
    return n < length ? n : length;
}

bool CH32RVUsbHost::networkLinkUp(uint8_t address) const
{
    return matches(address) && net.linkUp;
}

bool CH32RVUsbHost::networkStats(CH32RVUsbHostNetworkStats &stats, uint8_t address) const
{
    if (!matches(address)) {
        stats = CH32RVUsbHostNetworkStats();
        return false;
    }
    stats = net.stats;
    stats.ready = true;
    stats.linkUp = net.linkUp;
    return true;
}

#else /* no host */

bool CH32RVUsbHost::networkOpen(uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
void CH32RVUsbHost::networkClose(uint8_t) {}
bool CH32RVUsbHost::networkReady(uint8_t) const { return false; }
bool CH32RVUsbHost::getNetworkInterface(CH32RVUsbHostNetworkInterfaceInfo &) const { return false; }
bool CH32RVUsbHost::networkWriteFrame(const uint8_t *, size_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
size_t CH32RVUsbHost::networkReadFrame(uint8_t *, size_t, uint8_t) { return 0; }
bool CH32RVUsbHost::networkLinkUp(uint8_t) const { return false; }
bool CH32RVUsbHost::networkStats(CH32RVUsbHostNetworkStats &stats, uint8_t) const
{
    stats = CH32RVUsbHostNetworkStats();
    return false;
}

#endif
