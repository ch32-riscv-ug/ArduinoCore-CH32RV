/* The IP interface on a USB network device that CH32RVUsbHost drives
 * (CDC-NCM or CDC-ECM): CH32RVUsbHost::networkAttachNetif() makes and starts
 * it, and a sketch reaches it as usb.networkInterface() (a NetworkInterface:
 * hasIP(), localIP(), printTo() ...).
 *
 * Frames are pulled from the host stack's ring in poll() (the network task)
 * and sent with networkWriteFrame(), which waits for the bulk transfer. */
#pragma once
#include <CH32RVUsbHost.h>
#include "NetworkInterface.h"

class NetworkUsbHost : public NetworkInterface {
public:
    NetworkUsbHost(CH32RVUsbHost &host, uint8_t address);
    ~NetworkUsbHost();
    bool begin();
    void end();
    void poll() override;
    uint8_t address() const { return _address; }

protected:
    bool transmitFrame(const uint8_t *frame, size_t length) override;
    size_t printDriverInfo(Print &out) const override;

private:
    CH32RVUsbHost &_host;
    uint8_t _address;
    uint8_t *_frame = nullptr;
};
