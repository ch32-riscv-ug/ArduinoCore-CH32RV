/* The RTC as the wall clock, on the families with the F1-style RTC (V20x,
 * V205, V30x, L103 / M103): time() / gettimeofday() / settimeofday()
 * read and write it (syscalls.c), so the time survives a reset and standby
 * while the chip keeps power, and the low-power code wakes on its alarm.
 *
 *   - 1 Hz from LSI. LSI is a few percent off its nominal (a V307's ran at
 *     37 kHz for 40), so the first start measures it against the system
 *     clock and keeps the prescaler in the backup registers.
 *   - The counter is UNIX seconds. Under a second comes from the prescaler's
 *     divider, plus an offset settimeofday() keeps (the divider cannot be
 *     written), so gettimeofday() resolves milliseconds.
 *   - BKP DR1-DR4 are the core's: a marker, the prescaler (two words) and the
 *     offset. DR5 and up are free for sketches.
 *   - Once started the RTC keeps counting through every reset that leaves
 *     power on; a power cycle (with no VBAT) starts it again at 1970. */
#pragma once

#include <stdint.h>

/* V103 has the same RTC, but its first sleep on it never woke (2026-10-28,
 * V103R8T6 behind a CH549 WCH-Link that cannot cut the board's power) - left
 * out until that is understood. */
#if defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || \
    defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32V303) || defined(CH32RV_SERIES_CH32V305) || \
    defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317) || defined(CH32RV_SERIES_CH32L103) || \
    defined(CH32RV_SERIES_CH32M103)
#define CH32RV_HAS_RTC 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Start the RTC if it is not already running (idempotent; the first start
 * of all measures LSI, about 50 ms). 0 where there is none. */
int ch32rv_rtc_begin(void);
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

#ifdef __cplusplus
}
#endif
