/* A timer for the sketch: arduino-esp32 3.x's timer API (esp32-hal-timer),
 * on one whole TIM of the chip through CH32RVTimer.h's leases.
 *
 *     hw_timer_t *t = timerBegin(1000000);          // counts at 1 MHz, running
 *     timerAttachInterrupt(t, onAlarm);
 *     timerAlarm(t, 250, true, 0);                  // every 250 us
 *
 * timerBegin() takes the highest-numbered free timer that has a capture /
 * compare channel (the basic TIM6 / TIM7 have none), politely: it returns
 * NULL when analogWrite(), tone(), Servo or a library hold them all, and
 * when the frequency is out of the timer's reach (above its clock, or below
 * its clock / 65536). The counter is 64 bits as the ESP32's is (54 there):
 * the 16- or 32-bit TIM counts on and its overflows make the rest, so the
 * frequency is the counter's resolution, not a limit on the alarm. The alarm
 * is the timer's channel 1 in compare mode and runs the callback from the
 * timer's interrupt - keep it short, as on the ESP32 (IRAM there).
 *
 * The frequency is the timer clock divided by a whole number: one that does
 * not divide it is rounded to the nearest that does, and timerGetFrequency()
 * says what it is. Ticker (libraries/Ticker) runs on one of these.
 *
 * In stop and standby the timer stops with the clock: the count stalls. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hw_timer_s hw_timer_t;

hw_timer_t *timerBegin(uint32_t frequency);
void timerEnd(hw_timer_t *timer);

void timerStart(hw_timer_t *timer);
void timerStop(hw_timer_t *timer);
/* The count back to 0 (arduino-esp32's: running or not, it stays so). */
void timerRestart(hw_timer_t *timer);
void timerWrite(hw_timer_t *timer, uint64_t value);
uint64_t timerRead(hw_timer_t *timer);
uint64_t timerReadMicros(hw_timer_t *timer);
uint64_t timerReadMillis(hw_timer_t *timer);
double timerReadSeconds(hw_timer_t *timer);
uint32_t timerGetFrequency(hw_timer_t *timer);

void timerAttachInterrupt(hw_timer_t *timer, void (*userFunc)(void));
void timerAttachInterruptArg(hw_timer_t *timer, void (*userFunc)(void *), void *arg);
void timerDetachInterrupt(hw_timer_t *timer);
/* The callback runs when the count reaches alarm_value. autoreload: the
 * count then becomes reload_count and the alarm comes again each
 * alarm_value - reload_count ticks; else once. An alarm at or below the
 * count already reached runs at once. */
void timerAlarm(hw_timer_t *timer, uint64_t alarm_value, bool autoreload, uint64_t reload_count);

#ifdef __cplusplus
}
#endif
