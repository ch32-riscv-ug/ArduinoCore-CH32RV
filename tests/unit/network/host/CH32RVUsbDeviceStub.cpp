#include "CH32RVUsbDevice.h"
#include "link.h"

static CH32RVUsbDeviceNet *the_ncm;
static const uint8_t host_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x03};

CH32RVUsbDeviceNet::CH32RVUsbDeviceNet() {}

const uint8_t *CH32RVUsbDeviceNet::macAddress() const
{
    return host_mac;
}

bool CH32RVUsbDeviceNet::linkUp() const
{
    return the_ncm == this;
}

static void received(const uint8_t *frame, size_t length)
{
    the_ncm->handleFrame(frame, length);
}

static int ready()
{
    return the_ncm->receiveReady();
}

void CH32RVUsbDeviceNet::begin()
{
    the_ncm = this;
    link_open("tap0", received);
    link_rx_gate(ready);
}

void CH32RVUsbDeviceNet::handleFrame(const uint8_t *data, size_t length)
{
    if (_onFrame) {
        _onFrame(data, length);
    }
}

bool CH32RVUsbDeviceNet::sendFrame(const uint8_t *data, size_t length, uint32_t timeoutMs)
{
    (void)timeoutMs;
    ch32rv_usb_task();   /* as the real one: the USB stack runs here, and frames may arrive */
    if (!linkUp()) {
        return false;
    }
    link_send(data, length);
    return true;
}

/* The USB stack's task: the link's frames arrive from in here. */
extern "C" void ch32rv_usb_task(void)
{
    static bool running;
    if (the_ncm && !running) {
        running = true;
        link_poll();
        running = false;
    }
}
