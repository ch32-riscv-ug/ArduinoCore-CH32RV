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

#ifdef __cplusplus
}
#endif
