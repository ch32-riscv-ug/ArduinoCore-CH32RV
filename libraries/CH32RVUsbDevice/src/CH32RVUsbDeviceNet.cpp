/* USB network adapter: TinyUSB's ncm_device. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

#define NET_MTU 1514

#if CH32RV_TINYUSB_DEVICE && CFG_TUD_NCM

#include "class/net/net_device.h"

static const usbd_class_driver_t net_driver = {
    NULL, netd_init, netd_deinit, netd_reset, netd_open, netd_control_xfer_cb, netd_xfer_cb, NULL, NULL,
};

/* TinyUSB names the MAC through this global (iMACAddress). */
uint8_t tud_network_mac_address[6];
static bool mac_set;
static char mac_text[13];
static CH32RVUsbDeviceNet *net;

static const char *mac_string(uint8_t index)
{
    return index == CH32RV_USB_EXTRA_STRING ? mac_text : nullptr;
}

/* Two interfaces (control, data); notification IN on endpoint n, bulk OUT
 * and IN on n + 1. */
static uint16_t net_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *out, uint16_t room, uint8_t itf,
                               uint8_t *ep, uint8_t str, bool hs)
{
    (void)self;
    if (!mac_set) {
        /* Locally administered, unicast, from the 64-bit unique ID. */
        const uint32_t lo = *(volatile const uint32_t *)CH32RV_ESIG_UNIID1_ADDR;
        const uint32_t hi = *(volatile const uint32_t *)CH32RV_ESIG_UNIID2_ADDR;
        const uint32_t mix = lo ^ (hi * 2654435761u);
        tud_network_mac_address[0] = 0x02;
        tud_network_mac_address[1] = (uint8_t)(hi >> 8);
        tud_network_mac_address[2] = (uint8_t)(mix >> 24);
        tud_network_mac_address[3] = (uint8_t)(mix >> 16);
        tud_network_mac_address[4] = (uint8_t)(mix >> 8);
        tud_network_mac_address[5] = (uint8_t)mix;
    }
    static const char hex[] = "0123456789ABCDEF";
    for (uint8_t i = 0; i < 6; i++) {
        mac_text[2 * i] = hex[tud_network_mac_address[i] >> 4];
        mac_text[2 * i + 1] = hex[tud_network_mac_address[i] & 15];
    }
    mac_text[12] = 0;
    const uint8_t n = *ep;
    *ep = (uint8_t)(n + 2);
    uint8_t d[] = {TUD_CDC_NCM_DESCRIPTOR(itf, str, CH32RV_USB_EXTRA_STRING, (uint8_t)(0x80 | n), 64,
                                          (uint8_t)(n + 1), (uint8_t)(0x80 | (n + 1)),
                                          ch32rv_usb_bulk_size(hs), NET_MTU, ch32rv_usb_interval(hs, 16), 0)};
    /* The association names the function too, as for CDC. */
    d[7] = str;
    if (sizeof d > room) {
        return 0;
    }
    memcpy(out, d, sizeof d);
    return sizeof d;
}

static const CH32RVUsbFunctionType net_type = {&net_driver, net_descriptor, 2, 1, nullptr, 0};

CH32RVUsbDeviceNet::CH32RVUsbDeviceNet(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, net_type, CH32RV_USB_NET_NAME)
{
    if (attached()) {
        net = this;
        ch32rv_usb_extra_string = mac_string;
    }
}

void CH32RVUsbDeviceNet::macAddress(const uint8_t mac[6])
{
    memcpy(tud_network_mac_address, mac, 6);
    mac_set = true;
}

const uint8_t *CH32RVUsbDeviceNet::macAddress() const
{
    return tud_network_mac_address;
}

bool CH32RVUsbDeviceNet::linkUp() const
{
    return net == this && device().mounted();
}

bool CH32RVUsbDeviceNet::sendFrame(const uint8_t *data, size_t length, uint32_t timeoutMs)
{
    if (!data || !length || length > NET_MTU) {
        return false;
    }
    const uint32_t t0 = millis();
    for (;;) {
        if (!linkUp()) {
            return false;
        }
        if (tud_network_can_xmit((uint16_t)length)) {
            /* Copies through tud_network_xmit_cb() before it returns. */
            tud_network_xmit((void *)data, (uint16_t)length);
            return true;
        }
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return false;
        }
        ch32rv_usb_task();
    }
}

void CH32RVUsbDeviceNet::handleFrame(const uint8_t *data, size_t length)
{
    if (_onFrame) {
        _onFrame(data, length);
    }
}

extern "C" bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    if (net && src && size) {
        net->handleFrame(src, size);
    }
    /* Copied (or dropped) already: the buffer can take the next frame. */
    tud_network_recv_renew();
    return true;
}

extern "C" uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
    memcpy(dst, ref, arg);
    return arg;
}

extern "C" void tud_network_init_cb(void)
{
}

#else

static uint16_t net_descriptor(const CH32RVUsbDeviceFunction &, uint8_t *, uint16_t, uint8_t, uint8_t *, uint8_t, bool)
{
    return 0;
}

static const CH32RVUsbFunctionType net_type = {nullptr, net_descriptor, 2, 1, nullptr, 0};
static const uint8_t no_mac[6] = {};

CH32RVUsbDeviceNet::CH32RVUsbDeviceNet(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, net_type, CH32RV_USB_NET_NAME)
{
}

void CH32RVUsbDeviceNet::macAddress(const uint8_t *) {}
const uint8_t *CH32RVUsbDeviceNet::macAddress() const { return no_mac; }
bool CH32RVUsbDeviceNet::sendFrame(const uint8_t *, size_t, uint32_t) { return false; }
bool CH32RVUsbDeviceNet::linkUp() const { return false; }
void CH32RVUsbDeviceNet::handleFrame(const uint8_t *, size_t) {}

#endif
