#include "NetworkUsbNcm.h"
#include "NetworkCore.h"

#include <string.h>

#include "lwip/netif.h"

static const uint16_t QUEUE_BYTES = 4096;
/* One block from the host: 2 KB of frames, each queued with 2 bytes of length. */
static const uint16_t BLOCK_ROOM = 2048 + 128;
/* How long transmitFrame() waits for the one USB transmit buffer: a full
 * frame takes about 1.3 ms at full speed. */
static const uint32_t SEND_WAIT_MS = 5;

NetworkUsbNcm::NetworkUsbNcm(CH32RVUsbDeviceNet &ncm) : _ncm(ncm) {}

NetworkUsbNcm::~NetworkUsbNcm()
{
    end();
}

bool NetworkUsbNcm::begin(bool makeDefault)
{
    if (!_queue) {
        _queue = static_cast<uint8_t *>(malloc(QUEUE_BYTES));
        if (!_queue) {
            return false;
        }
    }
    _ncm.onFrame([this](const uint8_t *frame, size_t length) { queueFrame(frame, length); });
    /* No room for another block: the host waits rather than frames being dropped. */
    _ncm.receiveGate([this]() { return QUEUE_BYTES - _used >= BLOCK_ROOM; });
    /* The host's end of the link has the function's MAC address; this end
     * takes the same with the last bit flipped. */
    uint8_t mac[6];
    memcpy(mac, _ncm.macAddress(), 6);
    mac[5] ^= 1;
    const bool hadDefault = netif_default != nullptr;
    if (!netifStart(mac)) {
        return false;
    }
    if (!makeDefault && !hadDefault && isDefault()) {
        netif_set_default(nullptr);   /* reaches its own subnet only */
    }
    setLinkUp(_ncm.linkUp());
    return true;
}

void NetworkUsbNcm::end()
{
    _ncm.onFrame(nullptr);
    _ncm.receiveGate(nullptr);
    _ncm.resumeReceive();
    netifStop();
    free(_queue);
    _queue = nullptr;
    _used = 0;
}

/* Inside the USB stack: copy, never enter lwIP. */
void NetworkUsbNcm::queueFrame(const uint8_t *frame, size_t length)
{
    if (!_queue || length > 1514 || _used + 2 + length > QUEUE_BYTES) {
        _dropped++;
        return;
    }
    uint16_t tail = (uint16_t)((_head + _used) % QUEUE_BYTES);
    const uint8_t header[2] = {(uint8_t)length, (uint8_t)(length >> 8)};
    for (int i = 0; i < 2; i++) {
        _queue[tail] = header[i];
        tail = (uint16_t)((tail + 1) % QUEUE_BYTES);
    }
    const uint16_t first = (uint16_t)(QUEUE_BYTES - tail) < length ? (uint16_t)(QUEUE_BYTES - tail) : (uint16_t)length;
    memcpy(_queue + tail, frame, first);
    memcpy(_queue, frame + first, length - first);
    _used = (uint16_t)(_used + 2 + length);
}

void NetworkUsbNcm::poll()
{
    setLinkUp(_ncm.linkUp());
    while (_used >= 2) {
        const uint16_t length = (uint16_t)(_queue[_head] | _queue[(_head + 1) % QUEUE_BYTES] << 8);
        const uint16_t at = (uint16_t)((_head + 2) % QUEUE_BYTES);
        const uint16_t first = (uint16_t)(QUEUE_BYTES - at) < length ? (uint16_t)(QUEUE_BYTES - at) : length;
        /* lwIP copies the frame into a pbuf first thing, and its reply may run
         * the USB stack, which queues more behind this one - never over it, as
         * the frame is still counted in _used until it is handed over. */
        frameReceived(_queue + at, first, _queue, (size_t)(length - first));
        _head = (uint16_t)((at + length) % QUEUE_BYTES);
        _used = (uint16_t)(_used - 2 - length);
    }
    _ncm.resumeReceive();   /* if the gate had paused the host */
}

bool NetworkUsbNcm::transmitFrame(const uint8_t *frame, size_t length)
{
    return _ncm.sendFrame(frame, length, SEND_WAIT_MS);
}

size_t NetworkUsbNcm::printDriverInfo(Print &out) const
{
    size_t n = out.print("USB NCM");
    if (_dropped) {
        n += out.print(" dropped ");
        n += out.print(_dropped);
    }
    return n;
}
