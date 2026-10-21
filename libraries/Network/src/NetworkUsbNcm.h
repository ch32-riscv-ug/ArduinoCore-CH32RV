/* The IP interface on a USB network function (CDC-NCM, CH32RVUsbDeviceNet).
 * CH32RVUsbDeviceNet's EspUsbDevice-style calls (ipConfig(), dhcpServer(),
 * beginNetwork() ...) use it; a sketch can also use it directly:
 *
 *   CH32RVUsbDeviceNet ncm;
 *   NetworkUsbNcm usbnet(ncm);
 *   usbnet.config(IPAddress(192, 168, 7, 1), ...); usbnet.dhcpServer(true);
 *   USBDevice.begin(); usbnet.begin();
 *
 * Frames from the host are copied into a 4 KB queue inside the USB stack's
 * callback and handed to lwIP from the network task: the USB stack runs
 * inside sendFrame(), so lwIP must not be entered from its callbacks. While
 * the queue lacks room for another block, the USB function stops taking
 * blocks and the host waits (CH32RVUsbDeviceNet::receiveGate()). */
#pragma once
#include <CH32RVUsbDevice.h>
#include "NetworkInterface.h"

class NetworkUsbNcm : public NetworkInterface {
public:
    explicit NetworkUsbNcm(CH32RVUsbDeviceNet &ncm);
    ~NetworkUsbNcm();
    /* Starts the interface; the link follows the USB host. With makeDefault,
     * it becomes the default route when it is the first interface. */
    bool begin(bool makeDefault = false);
    void end();
    void poll() override;

protected:
    bool transmitFrame(const uint8_t *frame, size_t length) override;
    size_t printDriverInfo(Print &out) const override;

private:
    void queueFrame(const uint8_t *frame, size_t length);

    CH32RVUsbDeviceNet &_ncm;
    uint8_t *_queue = nullptr;         /* frames, each after a 2-byte length */
    uint16_t _head = 0, _used = 0;
    uint32_t _dropped = 0;
};
