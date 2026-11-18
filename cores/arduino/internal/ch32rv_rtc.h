/* The RTC as the wall clock, on the families with the F1-style RTC (V103,
 * V20x, V205, V30x, V4x7, L103 / M103, X305 / X315, H41x): time() /
 * gettimeofday() / settimeofday() read and write it (syscalls.c), so the time
 * survives a reset and standby while the chip keeps power, and the low-power
 * code wakes on its alarm.
 *
 *   - 1 Hz from LSI. LSI is a few percent off its nominal (a V307's ran at
 *     37 kHz for 40), so the first start measures it against the system
 *     clock and keeps the prescaler in the backup registers.
 *   - The counter is UNIX seconds. Under a second comes from the prescaler's
 *     divider, plus an offset settimeofday() keeps (the divider cannot be
 *     written), so gettimeofday() resolves milliseconds.
 *   - BKP DR1-DR4 are the core's: a marker, the prescaler (two words) and the
 *     offset (DR4's low 10 bits; its top bit says a standby was ended by the
 *     sketch's alarm). DR5 and up are the sketch's (ch32rv_backup_*()).
 *     CH32H41x and CH32X305 / X315 have the same RTC but no BKP data
 *     registers: the core's words and the sketch's are a .noinit record there,
 *     kept through every reset that leaves RAM powered (a power cycle loses
 *     the record, and the RTC starts over). X305 / X315 also have no backup
 *     domain: RTCEN and RTCSEL sit in the RTC's own CTLRL, and its alarm is
 *     EXTI line 20 (17 elsewhere).
 *   - The RTC is started (and the backup domain reset) only when it is not
 *     already counting LSI: the sketch's backup words written before the
 *     first time() survive it.
 *   - Once started the RTC keeps counting through every reset that leaves
 *     power on; a power cycle (with no VBAT) starts it again at 1970. */
#pragma once

#include <stdint.h>

/* V103 had been left out: its first sleep on the RTC never woke (2026-10-28,
 * V103R8T6 behind a CH549 WCH-Link that cannot cut the board's power). That
 * was before the alarm-0 fix of 2026-11-12 (a sleep in the RTC's first second
 * set no alarm and never woke - the same symptom on V203 / V307 / L103 / V205),
 * the likely cause; it is in again, to be confirmed on the bench. */
#if defined(CH32RV_SERIES_CH32V103) || defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || \
    defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32V303) || defined(CH32RV_SERIES_CH32V305) || \
    defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317) || defined(CH32RV_SERIES_CH32V407) || \
    defined(CH32RV_SERIES_CH32V467) || defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103) || \
    defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315) || \
    defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
#define CH32RV_HAS_RTC 1
#endif

#if defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315)
#define CH32RV_RTC_ALARM_LINE 20u
#define CH32RV_RTC_X3X5 1             /* RTCEN / RTCSEL in RTC_CTLRL, no BDCTLR, no DBP */
#else
#define CH32RV_RTC_ALARM_LINE 17u
#endif

#if defined(CH32RV_HAS_RTC) && (defined(CH32RV_RTC_X3X5) || defined(CH32RV_SERIES_CH32H415) || \
    defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417))
/* No BKP data registers: the core's words and the sketch's live in .noinit
 * RAM - kept through a reset, lost with power. */
#define CH32RV_RTC_STORE_RAM 1
#define CH32RV_BACKUP_WORDS 16u       /* what the sketch gets there */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Start the RTC if it is not already running (idempotent; the first start
 * of all measures LSI, about 50 ms; with the RTC Clock menu's LSE it starts
 * the crystal instead, up to 2 s, and counts LSI if that does not start).
 * 0 where there is none. */
int ch32rv_rtc_begin(void);
/* 1 when the running RTC counts LSE, the 32.768 kHz crystal. */
int ch32rv_rtc_on_lse(void);
/* The counter and the milliseconds under it - the RTC's own, without the
 * settimeofday() offset (the low-power code times sleeps with this). */
void ch32rv_rtc_raw(uint32_t *sec, uint32_t *ms);
/* Wall time: the counter plus the offset. */
void ch32rv_rtc_get(uint32_t *sec, uint32_t *ms);
void ch32rv_rtc_set(uint32_t sec, uint32_t ms);
/* The alarm: its flag rises when the counter reaches sec (EXTI line 17). */
void ch32rv_rtc_alarm(uint32_t sec);
/* After the clock stopped (a deep sleep): the registers' APB copies are
 * stale until they synchronise again. */
void ch32rv_rtc_resync(void);

/* The sketch's backup words (BKP DR5 and up, or the .noinit record): how
 * many, and read / write by index from 0. A write past the count is ignored,
 * a read gives 0. */
uint8_t ch32rv_backup_count(void);
uint16_t ch32rv_backup_read(uint8_t index);
int ch32rv_backup_write(uint8_t index, uint16_t value);

/* The sketch's alarm (ch32rv_rtc_alarm.c): wall-clock seconds, as time()
 * reads them; cb runs in the RTC alarm's interrupt within a second after
 * `when` (the RTC counts whole seconds; settimeofday()'s sub-second offset is
 * how late). 0 to clear. */
int ch32rv_rtc_user_alarm(uint32_t when, void (*cb)(void));
uint32_t ch32rv_rtc_user_alarm_at(void);     /* 0: none */
/* The RTC alarm's events (every one: the sketch's and the sleeps'). */
extern volatile uint32_t ch32rv_rtc_alarm_events;
/* Set by a sleep while it owns the alarm register. */
extern volatile uint8_t ch32rv_rtc_alarm_in_sleep;
/* For the sleeps: arm the alarm register for counter value `sec`, or the
 * sketch's alarm if that comes first; restore puts the sketch's back (and
 * runs its callback if it came while the register held the sleep's). */
void ch32rv_rtc_alarm_arm(uint32_t sec);
void ch32rv_rtc_alarm_restore(void);
/* settimeofday() moved the counter: the alarm register follows (weak: only
 * when the alarm code is linked). */
void ch32rv_rtc_alarm_rearm(void);
/* DR4's top bit (or the RAM record's): a standby ended by the sketch's alarm. */
void ch32rv_rtc_mark_alarm_standby(int on);
int ch32rv_rtc_take_alarm_standby(void);
/* The counter value at which wall second `when` has come (the alarm's). */
uint32_t ch32rv_rtc_counter_for(uint32_t when);

#ifdef __cplusplus
}
#endif
