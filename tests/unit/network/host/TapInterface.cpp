#include "TapInterface.h"
#include "link.h"
#include <string.h>

static TapInterface *the_tap;

static void received(const uint8_t *frame, size_t length);

bool TapInterface::begin(const char *ifname, const uint8_t mac[6])
{
    the_tap = this;
    memcpy(_mac, mac, 6);
    link_open(ifname, received);
    if (!netifStart(mac)) {
        return false;
    }
    setLinkUp(true);
    return true;
}

bool TapInterface::restart()
{
    netifStop();
    if (!netifStart(_mac)) {
        return true;
    }
    const bool early = linkUp();
    setLinkUp(true);
    return early;
}

void TapInterface::poll()
{
    link_poll();
}

bool TapInterface::transmitFrame(const uint8_t *frame, size_t length)
{
    link_send(frame, length);
    return true;
}

static void received(const uint8_t *frame, size_t length)
{
    the_tap->deliver(frame, length);
}
