/* The RTC as the wall clock (ch32rv_rtc.h). Brought up on a CH32V307VCT6
 * 2026-10-28: RTCEN does not stick when written together with RTCSEL. */
#include "Arduino.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_rtc.h"
#include "internal/ch32rv_backup.h"

#ifdef CH32RV_HAS_RTC

#if defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
/* H41x: one register more before it in RCC, and its fields lower (RTCEN 8,
 * RTCSEL 7:6; the reference manual's 3.4.10). */
#define RCC_BDCTLR      CH32RV_REG32(0x40021024u)
#define BDCTLR_RTCSEL   (3u << 6)
#define BDCTLR_LSI      (2u << 6)
#define BDCTLR_RTCEN    (1u << 8)
#elif !defined(CH32RV_RTC_X3X5)
#define RCC_BDCTLR      CH32RV_REG32(0x40021020u)
#define BDCTLR_RTCSEL   (3u << 8)
#define BDCTLR_LSI      (2u << 8)
#define BDCTLR_RTCEN    (1u << 15)
#endif
#define BDCTLR_BDRST    (1u << 16)
#define PWR_CTLR        CH32RV_REG32(0x40007000u)
#define PWR_CTLR_DBP    (1u << 8)
#define RTC(o)          CH32RV_REG16(0x40002800u + (o))
#define RTC_CTLRL_ALRF  (1u << 1)
#define RTC_CTLRL_RSF   (1u << 3)
#define RTC_CTLRL_CNF   (1u << 4)
#define RTC_CTLRL_RTOFF (1u << 5)
#define RTC_CTLRH_ALRIE (1u << 1)
#ifdef CH32RV_RTC_X3X5
/* X305 / X315: the clock's choice and enable in CTLRL itself (RTCEN bit 6,
 * RTCSEL bit 7 - 0 is LSI; the reference manual's 6.3.2), a 32-bit register. */
#define RTC_CTLRL32     CH32RV_REG32(0x40002804u)
#define CTLRL_RTCEN     (1u << 6)
#define CTLRL_RTCSEL    (1u << 7)
#endif
#define BKP_MARKER      0xC32Bu
#define DR4_OFFSET      0x03FFu      /* settimeofday()'s milliseconds */
#define DR4_ALARM_SB    0x8000u      /* a standby the sketch's alarm ended */
#define BKP_DR(n)       CH32RV_RTC_DR(n)

/* The alarm code's, linked only with it (ch32rv_rtc_alarm.c). */
void ch32rv_rtc_alarm_rearm(void) __attribute__((weak));

static uint8_t rtc_up;
static uint32_t rtc_psc;        /* prescaler: RTCCLK ticks per second - 1 */

static void wait_off(void)
{
    for (uint32_t spin = 0; !(RTC(0x04) & RTC_CTLRL_RTOFF) && spin < 1000000u; spin++) {
    }
}

static void config_begin(void)
{
    wait_off();
    RTC(0x04) |= RTC_CTLRL_CNF;
}

static void config_end(void)
{
    RTC(0x04) &= ~RTC_CTLRL_CNF;
    wait_off();
}

void ch32rv_rtc_resync(void)
{
    RTC(0x04) &= ~RTC_CTLRL_RSF;
    for (uint32_t spin = 0; !(RTC(0x04) & RTC_CTLRL_RSF) && spin < 1000000u; spin++) {
    }
}

static uint32_t read_div(void)
{
    return ((uint32_t)(RTC(0x10) & 0xFu) << 16) | RTC(0x14);
}

/* LSI against micros(): how far the divider counts down in 50 ms, with the
 * prescaler at its largest so that it does not reload meanwhile. */
static uint32_t measure_lsi(void)
{
    config_begin();
    RTC(0x08) = 0xF;
    RTC(0x0C) = 0xFFFF;
    config_end();
    ch32rv_rtc_resync();
    const uint32_t d0 = read_div(), t0 = micros();
    while (micros() - t0 < 50000u) {
    }
    const uint32_t d1 = read_div(), t1 = micros();
    const uint32_t ticks = (d0 - d1) & 0xFFFFFu;  /* about 2000: x 10^6 still fits */
    return ticks * 1000000u / (t1 - t0);
}

/* Whether the RTC already counts LSI (an earlier boot started it). */
static int rtc_on_lsi(void)
{
#ifdef CH32RV_RTC_X3X5
    return (RTC_CTLRL32 & (CTLRL_RTCEN | CTLRL_RTCSEL)) == CTLRL_RTCEN;
#else
    return (RCC_BDCTLR & (BDCTLR_RTCSEL | BDCTLR_RTCEN)) == (BDCTLR_LSI | BDCTLR_RTCEN);
#endif
}

/* LSI as the RTC's clock, and the clock on. */
static void rtc_clock_lsi(void)
{
#ifdef CH32RV_RTC_X3X5
    RTC_CTLRL32 &= ~CTLRL_RTCSEL;
    RTC_CTLRL32 |= CTLRL_RTCEN;
#else
    /* The backup domain is reset only when RTCSEL holds another clock (it
     * cannot be changed otherwise): after a power-up it is 0 and written
     * as it is, so the sketch's backup words written before the RTC's
     * first start survive it. */
    if ((RCC_BDCTLR & BDCTLR_RTCSEL) != 0u && (RCC_BDCTLR & BDCTLR_RTCSEL) != BDCTLR_LSI) {
        RCC_BDCTLR |= BDCTLR_BDRST;
        RCC_BDCTLR &= ~BDCTLR_BDRST;
    }
    /* Each field read back before the next write: on CH32H417 RTCSEL written
     * right after the reset was lost (RTCEN alone stuck, the RTC had no clock;
     * its EVT waits ten NOPs between the two, 2026-11-14). */
    for (uint32_t spin = 0; (RCC_BDCTLR & BDCTLR_RTCSEL) != BDCTLR_LSI && spin < 1000u; spin++) {
        RCC_BDCTLR = (RCC_BDCTLR & ~BDCTLR_RTCSEL) | BDCTLR_LSI;
    }
    RCC_BDCTLR |= BDCTLR_RTCEN;                /* a write of its own, or it does not stick */
    for (uint32_t spin = 0; !(RCC_BDCTLR & BDCTLR_RTCEN) && spin < 1000u; spin++) {
        RCC_BDCTLR |= BDCTLR_RTCEN;
    }
#endif
}

int ch32rv_rtc_begin(void)
{
    if (rtc_up) {
        return 1;
    }
    ch32rv_rtc_domain_open();
    CH32RV_RCC_RSTSCKR |= 1u;                  /* LSION: not kept by a reset */
    for (uint32_t spin = 0; !(CH32RV_RCC_RSTSCKR & 2u) && spin < 1000000u; spin++) {
    }
    if (rtc_on_lsi() && BKP_DR(1) == BKP_MARKER) {
        /* Running since an earlier boot: leave it counting. */
        rtc_psc = BKP_DR(2) | ((uint32_t)BKP_DR(3) << 16);
        ch32rv_rtc_resync();
        rtc_up = 1;
        return 1;
    }
    rtc_clock_lsi();
    ch32rv_rtc_resync();
    const uint32_t lsi = measure_lsi();
    rtc_psc = (lsi ? lsi : 40000u) - 1u;
    config_begin();
    RTC(0x08) = (uint16_t)(rtc_psc >> 16);
    RTC(0x0C) = (uint16_t)rtc_psc;
    RTC(0x18) = 0;
    RTC(0x1C) = 0;
    config_end();
    BKP_DR(2) = (uint16_t)rtc_psc;
    BKP_DR(3) = (uint16_t)(rtc_psc >> 16);
    BKP_DR(4) = 0;
    BKP_DR(1) = BKP_MARKER;
    ch32rv_rtc_domain_seal();
    rtc_up = 1;
    return 1;
}

void ch32rv_rtc_raw(uint32_t *sec, uint32_t *ms)
{
    uint32_t s, d;
    /* The counter and the divider are two reads: take them again until the
     * counter has not moved in between. */
    do {
        s = ((uint32_t)RTC(0x18) << 16) | RTC(0x1C);
        d = read_div();
    } while (s != (((uint32_t)RTC(0x18) << 16) | RTC(0x1C)));
    *sec = s;
    *ms = (rtc_psc - (d > rtc_psc ? rtc_psc : d)) * 1000u / (rtc_psc + 1u);
}

void ch32rv_rtc_get(uint32_t *sec, uint32_t *ms)
{
    uint32_t s, m;
    ch32rv_rtc_raw(&s, &m);
    m += BKP_DR(4) & DR4_OFFSET;
    if (m >= 1000u) {
        m -= 1000u;
        s++;
    }
    *sec = s;
    *ms = m;
}

void ch32rv_rtc_set(uint32_t sec, uint32_t ms)
{
    uint32_t s, m;
    ch32rv_rtc_raw(&s, &m);
    /* counter x 1000 + m + offset = sec x 1000 + ms, the offset under 1000. */
    int32_t off = (int32_t)ms - (int32_t)m;
    uint32_t cnt = sec;
    if (off < 0) {
        off += 1000;
        cnt--;
    }
    config_begin();
    RTC(0x18) = (uint16_t)(cnt >> 16);
    RTC(0x1C) = (uint16_t)cnt;
    config_end();
    BKP_DR(4) = (uint16_t)((BKP_DR(4) & ~DR4_OFFSET) | (uint16_t)off);
    ch32rv_rtc_domain_seal();
    if (ch32rv_rtc_alarm_rearm) {
        ch32rv_rtc_alarm_rearm();              /* the sketch's alarm is wall time */
    }
}

uint32_t ch32rv_rtc_counter_for(uint32_t when)
{
    /* The counter reaches c at wall time c + offset: the first c at or after
     * `when` is `when` itself, the offset (under a second) late. */
    return when;
}

void ch32rv_rtc_mark_alarm_standby(int on)
{
    BKP_DR(4) = (uint16_t)(on ? (BKP_DR(4) | DR4_ALARM_SB) : (BKP_DR(4) & ~DR4_ALARM_SB));
    ch32rv_rtc_domain_seal();
}

int ch32rv_rtc_take_alarm_standby(void)
{
    ch32rv_rtc_domain_open();
    if (BKP_DR(1) != BKP_MARKER || !(BKP_DR(4) & DR4_ALARM_SB)) {
        return 0;
    }
    ch32rv_rtc_mark_alarm_standby(0);
    return 1;
}

void ch32rv_rtc_alarm(uint32_t sec)
{
    /* The flag rises one count after the alarm value (V307 2026-10-28: a
     * 2 s sleep took 3), so the register holds one less. Not on V103: its
     * flag rises as the counter reaches the value, as on the F1 (2026-11-15:
     * an alarm written one less never came for a counter already there - the
     * first-sleep hang of 2026-10-28 - and a 2 s alarm came a second early). */
#ifndef CH32RV_SERIES_CH32V103
    sec--;
#endif
    config_begin();
    RTC(0x20) = (uint16_t)(sec >> 16);
    RTC(0x24) = (uint16_t)sec;
    config_end();
    RTC(0x04) &= ~RTC_CTLRL_ALRF;
    RTC(0x00) |= RTC_CTLRH_ALRIE;
}

#else

int ch32rv_rtc_begin(void)
{
    return 0;
}

#endif
