#include "NetworkCore.h"
#include "NetworkInterface.h"

#include "lwip/init.h"
#include "lwip/timeouts.h"

namespace ch32rv_net {

static bool started;
static NetworkInterface *interfaces[4];

void start()
{
    if (!started) {
        started = true;
        lwip_init();
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

/* main() runs it after every loop(), and delay() while it waits. Reentry - a
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
    sys_check_timeouts();
    running = false;
}

extern "C" uint32_t sys_now(void)
{
    return millis();
}

/* TCP's initial sequence numbers and the DHCP / DNS transaction ids: not
 * cryptographic, but different on every chip and every start. A sketch, or a
 * driver with a hardware generator, can replace it. */
extern "C" __attribute__((weak)) uint32_t network_random(void)
{
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
