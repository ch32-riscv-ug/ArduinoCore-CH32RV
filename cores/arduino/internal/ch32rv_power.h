/* Deep sleep that keeps RAM and resumes where it stopped - the engine behind
 * CH32RV.lightSleep() / deepSleep() (libraries/CH32RV). docs/peripheral-a-
 * design.ja.md 4.3 and 5.1 hold the rules and the measurements.
 *
 *   - The mode is the deepest one that keeps RAM: V003 / V00x and M030
 *     "standby" (RAM kept, resumes), X035 standby (PDDS; its stop resumes as
 *     well), stop on the RTC families. Every family sleeps now
 *     (ch32rv_power_supported()).
 *   - Wake sources: the AWU (a timer: LSI on V00x, measured against the
 *     system clock once per boot; HSI / 1024 on X035), EXTI lines set up
 *     by ch32rv_exti_configure() (pins) and the other EXTI lines
 *     (ch32rv_exti_line.h: the PVD, USB wake-ups, the RTC alarm).
 *   - millis() stops while asleep; the time the AWU measured is added back.
 *     A wake whose sleep length is not known (a pin, part-way through an AWU
 *     period) advances ch32rv_time_domain instead of guessing.
 *   - A running IWDG cannot be stopped (measured): wdt_slice_ms bounds each
 *     AWU period, and the IWDG is fed between them - nothing else runs.
 *
 * Normal context only, interrupts enabled. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CH32RV_POWER_WOKE_TIMER = 1,
    CH32RV_POWER_WOKE_PIN = 2,
    CH32RV_POWER_WOKE_ALARM = 3,    /* the sketch's RTC alarm (enableAlarmWakeup()) */
    CH32RV_POWER_WOKE_EXTI = 4,     /* an attachExtiLine() line (enableExtiLineWakeup()) */
} CH32RVPowerWake;

/* Bumped by every wake whose sleep length is not known: millis() then left
 * out an unknown stretch, and a deadline across it is not to be trusted. */
extern volatile uint32_t ch32rv_time_domain;

/* What ended the last sleep: 0 (none yet: a reset), or a CH32RVPowerWake.
 * Kept across deepSleep()'s restart. */
extern volatile uint8_t ch32rv_wake_cause;

int ch32rv_power_supported(void);

/* The wake sources set up so far (ch32rv_power.c; pins by ch32rv_power_pin.c). */
struct ch32rv_power_wake {
    uint32_t timer_ms;
    uint8_t pins;
    uint8_t alarm;                  /* the sketch's RTC alarm wakes it too */
    int16_t pin[4];
    uint8_t level[4];
    uint32_t lines;                 /* EXTI lines that are not pins (ch32rv_power_exti.c) */
};
extern struct ch32rv_power_wake ch32rv_power_wake;

/* The wake sources the next sleep waits for. 0 where the family cannot sleep
 * (yet) or the pin has no EXTI line / four are taken. */
int ch32rv_power_set_timer(uint64_t us);
int ch32rv_power_add_pin(int pin, int level);
/* The sketch's RTC alarm (ch32rv_rtc_user_alarm()) as a wake source; 0 where
 * the family wakes on the AWU (no RTC). */
int ch32rv_power_set_alarm(void);
/* An attachExtiLine() line (ch32rv_exti_line.h) as a wake source, its edge as
 * mode says (RISING / FALLING / CHANGE), keeping its callback; 0 where the
 * family cannot sleep or the series has no such line. */
int ch32rv_power_add_line(uint8_t line, int mode);
void ch32rv_power_clear(void);

/* lightSleep(): 0 slept and woke, 1 nothing to wake on, 2 not on this
 * family. A pin already at its level is a wake without sleeping. */
int ch32rv_power_sleep(uint32_t wdt_slice_us);

/* deepSleep() on the RTC families: standby when it is woken by the timer or
 * the alarm alone (pins other than WKUP cannot wake it). Returns only if it
 * cannot. */
int ch32rv_power_can_standby(void);
void ch32rv_power_standby(void);

/* deepSleep(): keep the cause for the next boot (ch32rv_power_boot()). */
void ch32rv_power_keep_cause(void);
/* Runs before setup() (a constructor in ch32rv_power.c): takes the kept cause. */
void ch32rv_power_boot(void);
/* The longest single AWU period, in microseconds. */
uint32_t ch32rv_power_awu_max_us(void);

/* Sleep until timer_ms has passed (0: no timer) or one of pin_lines (EXTI
 * line bits, 0: none) fires. wdt_slice_us: 0, or the longest stretch the
 * running IWDG may go unfed - it is fed between periods of at most that.
 * Returns the wake (0 when there was nothing to wait for); *slept_ms is what
 * was measured (millis() has already been advanced by it). */
int ch32rv_power_light_sleep(uint32_t timer_ms, uint32_t pin_lines, uint32_t wdt_slice_us,
                           uint32_t *slept_ms);

#ifdef __cplusplus
}
#endif
