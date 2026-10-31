#include "NetworkCore.h"
#include "NetworkInterface.h"

#ifdef CH32RV_RNG_BASE
#include <ch32rv_rng.h>
#endif

#include "ch32rv_service.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"

namespace ch32rv_net {

static bool started;
static NetworkInterface *interfaces[4];

/* Every entry point of the library comes through here, so this is where the
 * IP stack joins the system services that delay() and the return of loop()
 * run (ch32rv_service.h). */
void start()
{
    if (!started) {
        started = true;
        lwip_init();
        static ch32rv_service_t service = CH32RV_SERVICE_INIT(ch32rv_net_task);
        ch32rv_service_attach(&service);
    }
}

void pump()
{
    if (ch32rv_usb_task) {
        ch32rv_usb_task();
    }
    if (ch32rv_usb_host_task) {
        ch32rv_usb_host_task();
    }
    ch32rv_net_task();
    yield();
}

bool waitFor(bool (*done)(void *arg), void *arg, uint32_t timeoutMs)
{
    const uint32_t start = millis();
    while (!done(arg)) {
        if ((uint32_t)(millis() - start) >= timeoutMs) {
            return false;
        }
        pump();
    }
    return true;
}

static NetifListener netif_listener;

void setNetifListener(NetifListener listener)
{
    netif_listener = listener;
}

void netifChanged(struct netif *n, bool up)
{
    if (netif_listener) {
        netif_listener(n, up);
    }
}

void addInterface(NetworkInterface *ifc)
{
    for (auto &slot : interfaces) {
        if (slot == ifc) {
            return;
        }
    }
    for (auto &slot : interfaces) {
        if (!slot) {
            slot = ifc;
            return;
        }
    }
}

void removeInterface(NetworkInterface *ifc)
{
    for (auto &slot : interfaces) {
        if (slot == ifc) {
            slot = nullptr;
        }
    }
}

NetworkInterface *firstInterface()
{
    for (auto *slot : interfaces) {
        if (slot) {
            return slot;
        }
    }
    return nullptr;
}

}  // namespace ch32rv_net

/* The IP stack's system service (start() attaches it): delay() and the return
 * of loop() run it, and so do the library's own waits (pump()). Reentry - a
 * callback of ours that pumps - is cut off here: lwIP is not reentrant. */
extern "C" void ch32rv_net_task(void)
{
    static bool running;
    if (running) {
        return;
    }
    running = true;
    ch32rv_net::start();
    for (auto *ifc : ch32rv_net::interfaces) {
        if (ifc) {
            ifc->poll();
        }
    }
    netif_poll_all();   /* what was sent to 127.0.0.1 or an own address */
    sys_check_timeouts();
    running = false;
}

extern "C" uint32_t sys_now(void)
{
    return millis();
}

/* TCP's initial sequence numbers and the DHCP / DNS transaction ids: from the
 * chip's random number generator where there is one; otherwise not
 * cryptographic, but different on every chip and every start. A sketch can
 * replace it. */
extern "C" __attribute__((weak)) uint32_t network_random(void)
{
#ifdef CH32RV_RNG_BASE
    uint32_t r;
    if (ch32rv_rng_read(&r, sizeof r)) {
        return r;
    }
#endif
    static uint32_t state;
    if (!state) {
#ifdef CH32RV_ESIG_UNIID1_ADDR
        state = *(volatile const uint32_t *)CH32RV_ESIG_UNIID1_ADDR ^ (*(volatile const uint32_t *)CH32RV_ESIG_UNIID2_ADDR * 2654435761u);
#endif
        state ^= micros() * 0x9E3779B9u;
        if (!state) {
            state = 0x6D2B79F5u;
        }
    }
    state ^= micros();
    if (!state) {
        state = 0x6D2B79F5u;
    }
    /* xorshift32 */
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
