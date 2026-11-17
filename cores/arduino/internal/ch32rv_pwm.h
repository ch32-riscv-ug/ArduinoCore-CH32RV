/* Between wiring_pwm.c (the default pads, and what every analogWrite()
 * shares) and wiring_pwm_any.c (the pad table's analogWrite()). */
#pragma once

#include "CH32RVTimer.h"
#include "api/Common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t ch32rv_pwm_write_bits;
/* Each timer channel's PWM lease and the pad it drives; a lease's timer 0 is
 * an empty slot. */
extern CH32RVTimerLease ch32rv_pwm_lease[CH32RV_TIMER_COUNT][4];
/* The lease of the pad's PWM, or 0 (weak in wiring_pwm.c). */
CH32RVTimerLease *ch32rv_pwm_slot_of(pin_size_t pin);

bool ch32rv_pwm_dac(pin_size_t pin, int value);
void ch32rv_pwm_level(pin_size_t pin, int high);
void ch32rv_pwm_start(pin_size_t pin, int value, const CH32RVTimerPad *pad, int index);
/* Weak in wiring_pwm.c, wiring_pwm_any.c's where the pad table is linked:
 * the pads `pad` moves off its timer stop, and the pad is recorded. */
void ch32rv_pwm_make_room(const CH32RVTimerPad *pad, int index);

#ifdef __cplusplus
}
#endif
