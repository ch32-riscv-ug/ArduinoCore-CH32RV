/* CH32H41x's two cores, V5F and V3F: exclusion (the hardware semaphore, HSEM) and notification (the inter-core
 * channel, IPC). The same functions on both cores; CH32RV.hsem*() / CH32RV.ipc*() call these.
 *
 *     #include <ch32rv_multicore.h>
 *
 * HSEM (CH32RV_HAS_HSEM): channels 0 to CH32RV_HSEM_USER_CHANNELS - 1 (30); 31 is the core's own lock.
 *  - ch32rv_hsem_try_take(ch): takes the channel if it is free - 1 - or leaves it - 0. One read of the HSEM, no
 *    waiting.
 *  - ch32rv_hsem_take(ch, timeout_ms): tries until it has it or timeout_ms has passed (CH32RV_WAIT_FOREVER: no
 *    limit; 0: once). 0 at once when this core holds the channel already: no wait could end it.
 *  - ch32rv_hsem_release(ch): gives back a channel this core holds - 1 - or does nothing - 0: the other core's
 *    channels and free ones are left as they are (the HSEM releases only for the core that took it).
 *  - A channel belongs to a core, not to a function: an interrupt handler on the same core cannot take what its
 *    loop holds (try_take is 0), and it must not wait for it.
 *
 * IPC (CH32RV_HAS_IPC): one 32-bit message at a time in each direction, through the IPC's channel 0 (to the V5F)
 * and channel 1 (to the V3F) - status bit 0 says a message waits, MSG0 / MSG1 hold it. Channels 2 and 3 are left
 * to the sketch.
 *  - ch32rv_ipc_send(msg, timeout_ms): to the other core. Waits while its last message has not been taken (up to
 *    timeout_ms, CH32RV_WAIT_FOREVER or 0 as above); 1 once the message is in, 0 if the time ran out. From an
 *    interrupt handler pass 0. 0 at once where no other core runs the sketch's code (CPU Core = V3F, where the
 *    V5F stays asleep).
 *  - ch32rv_ipc_on_receive(cb): cb(msg) runs in this core's IPC interrupt for each message to it (nullptr stops
 *    it). The interrupt is the core's own (IPC_CH0 on the V5F, IPC_CH1 on the V3F), so it runs where cb was set.
 *  - ch32rv_ipc_receive(&msg): takes a waiting message without a callback - 1 - or 0 when none waits.
 *  - The other core need not be running yet: a message waits for it (and the sender's next one waits behind it).
 *
 * Elsewhere none of these is declared. */
#pragma once

#include <stdint.h>

#include "pins_arduino.h"
#include "internal/ch32rv_registers.h"

#ifndef CH32RV_WAIT_FOREVER
#define CH32RV_WAIT_FOREVER 0xFFFFFFFFu
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CH32RV_IRQN_HSEM)
#define CH32RV_HAS_HSEM 1
#define CH32RV_HSEM_USER_CHANNELS CH32RV_HSEM_CORE_CHANNEL
int ch32rv_hsem_try_take(uint32_t channel);
int ch32rv_hsem_take(uint32_t channel, uint32_t timeout_ms);
int ch32rv_hsem_release(uint32_t channel);
#endif

#if defined(CH32RV_IRQN_IPC_CH0) && defined(CH32RV_IRQN_IPC_CH1)
#define CH32RV_HAS_IPC 1
int ch32rv_ipc_send(uint32_t message, uint32_t timeout_ms);
int ch32rv_ipc_receive(uint32_t *message);
int ch32rv_ipc_on_receive(void (*callback)(uint32_t message));
#endif

#ifdef __cplusplus
}
#endif
