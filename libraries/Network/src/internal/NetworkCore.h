/* The Network library's internals, shared by its classes: lwIP's start, the
 * background task, and waiting for the stack without blocking it.
 *
 * Everything in lwIP runs in task context - from ch32rv_net_task(), which
 * main() runs after every loop() and delay() runs while it waits, or from a
 * blocking call of this library that pumps it (connect(), write(),
 * hostByName() ...). Never from an interrupt. */
#pragma once
#include <Arduino.h>

class NetworkInterface;
struct netif;

namespace ch32rv_net {

/* lwIP's one-time initialisation. Every entry point calls it. */
void start();

/* One round of the background work (USB, then the IP stack), for a blocking
 * call that waits on the network. */
void pump();

/* Pump until done(arg) or timeoutMs; true when done. */
bool waitFor(bool (*done)(void *arg), void *arg, uint32_t timeoutMs);

/* A listener for interfaces coming and going (the mDNS responder): called
 * with each netif as it starts, and before it stops. One listener; nullptr
 * removes it. */
typedef void (*NetifListener)(struct netif *n, bool up);
void setNetifListener(NetifListener listener);
void netifChanged(struct netif *n, bool up);

/* The interfaces that have started, for the task to poll. */
void addInterface(NetworkInterface *ifc);
void removeInterface(NetworkInterface *ifc);
NetworkInterface *firstInterface();

}  // namespace ch32rv_net
