/* A NetworkInterface on a Linux tap device, with an emulated link between the
 * two (rate, delay, loss, reordering; link.h), for the host tests. */
#pragma once
#include <NetworkInterface.h>

class TapInterface : public NetworkInterface {
public:
    bool begin(const char *ifname, const uint8_t mac[6]);
    void poll() override;
    /* link.c's receive callback lands here (inside poll()). */
    void deliver(const uint8_t *frame, size_t length) { frameReceived(frame, length); }

protected:
    bool transmitFrame(const uint8_t *frame, size_t length) override;
};
