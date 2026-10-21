/* Host bench for embedded IP stacks: a tap device inside a user network
 * namespace, with an emulated link (rate, delay, loss, reorder) applied the
 * same way in both directions so every stack sees identical conditions.
 * Env: LINK_KBPS (0 = unlimited), LINK_DELAY_US, LINK_LOSS_PPM, LINK_REORDER_PPM, LINK_SEED. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*link_rx_t)(const uint8_t *frame, size_t len);
void link_open(const char *ifname, link_rx_t rx);
void link_send(const uint8_t *frame, size_t len);      /* from the stack to the host */
void link_poll(void);                                  /* moves frames both ways; call often */
uint32_t link_ms(void);
uint64_t link_us(void);
void link_stats(void);
#ifdef __cplusplus
}
#endif
/* The test sketch's data: byte i of a stream. */
#define PATTERN(i) ((uint8_t)((i) * 7u + ((i) >> 9)))
#define SOURCE_BYTES (1u << 20)
