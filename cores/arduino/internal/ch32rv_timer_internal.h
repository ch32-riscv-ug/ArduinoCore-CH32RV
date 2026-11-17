/* Between ch32rv_timer.c (ownership) and ch32rv_timer_irq.c (the update
 * interrupt), which only a sketch attaching an update interrupt links. */
#pragma once

#include "CH32RVTimer.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const CH32RVTimerCapability ch32rv_timer_caps[];
/* The index of timer `timer` in ch32rv_timer_caps, -1 when there is none. */
int ch32rv_timer_index(uint8_t timer);
/* Drop the update interrupt's owner: the one with `identity`, or whoever it
 * is with 0. Weak no-ops in ch32rv_timer.c when ch32rv_timer_irq.c is not
 * linked. */
void ch32rv_timer_update_forget(size_t index, const void *identity);
bool ch32rv_timer_update_owned(size_t index);
/* The capture / compare interrupt's owner, likewise (ch32rv_timer_cc.c over
 * weak no-ops in ch32rv_timer.c and ch32rv_timer_irq.c). The dispatch runs
 * from the vector a timer's channels share with its update. */
void ch32rv_timer_channel_forget(size_t index, const void *identity);
void ch32rv_timer_channel_dispatch(size_t index);
void ch32rv_timer_irq_link(void);
/* The timer's AFIO route (ch32rv_timer_pad.c; a weak no-op in wiring_pwm.c
 * for sketches that never move a timer off its reset route). */
void ch32rv_timer_route_set(uint8_t timer, uint8_t route);
uint8_t ch32rv_timer_pad_first_timer(uint32_t pin);

#ifdef __cplusplus
}
#endif
