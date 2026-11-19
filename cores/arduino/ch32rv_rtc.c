/* The RTC as the wall clock (ch32rv_rtc.h). Brought up on a CH32V307VCT6
 * 2026-10-28: RTCEN does not stick when written together with RTCSEL. */
#include "Arduino.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_rtc.h"
#include "internal/ch32rv_backup.h"
#include "internal/ch32rv_irq.h"

#ifdef CH32RV_HAS_RTC

#if defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
/* H41x: one register more before it in RCC, and its fields lower (RTCEN 8,
 * RTCSEL 7:6; the reference manual's 3.4.10). */
#define RCC_BDCTLR      CH32RV_REG32(0x40021024u)
#define BDCTLR_RTCSEL   (3u << 6)
#define BDCTLR_LSE      (1u << 6)
#define BDCTLR_LSI      (2u << 6)
#define BDCTLR_RTCEN    (1u << 8)
#elif !defined(CH32RV_RTC_X3X5)
#define RCC_BDCTLR      CH32RV_REG32(0x40021020u)
#define BDCTLR_RTCSEL   (3u << 8)
#define BDCTLR_LSE      (1u << 8)
#define BDCTLR_LSI      (2u << 8)
#define BDCTLR_RTCEN    (1u << 15)
#endif
/* LSE, the 32.768 kHz crystal on OSC32_IN / OUT: on, and ready (every
 * family with BDCTLR; the RTC Clock menu's choice, CH32RV_RTC_LSE). */
#define BDCTLR_LSEON    (1u << 0)
#define BDCTLR_LSERDY   (1u << 1)
#if defined(CH32RV_RTC_LSE) && defined(CH32RV_RTC_X3X5)
#error the RTC Clock menu has no LSE on CH32X305 / X315
#endif
/* How long the crystal gets to start: typically under a second; 2 s is the
 * common worst case for a 32.768 kHz crystal at start-up. */
#define LSE_START_MS    2000u
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
static uint8_t rtc_lse;         /* the RTC counts LSE (else LSI) */
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

#ifdef CH32RV_RTC_X3X5
/* Whether the RTC already counts LSI (an earlier boot started it). */
static int rtc_on_lsi(void)
{
    return (RTC_CTLRL32 & (CTLRL_RTCEN | CTLRL_RTCSEL)) == CTLRL_RTCEN;
}

/* LSI as the RTC's clock, and the clock on. */
static void rtc_clock_lsi(void)
{
    RTC_CTLRL32 &= ~CTLRL_RTCSEL;
    RTC_CTLRL32 |= CTLRL_RTCEN;
}
#else
/* Whether the RTC already counts `sel` (an earlier boot started it). */
static int rtc_on(uint32_t sel)
{
    return (RCC_BDCTLR & (BDCTLR_RTCSEL | BDCTLR_RTCEN)) == (sel | BDCTLR_RTCEN);
}
#define rtc_on_lsi() rtc_on(BDCTLR_LSI)

/* `sel` as the RTC's clock, and the clock on. */
static void rtc_clock(uint32_t sel)
{
    /* The backup domain is reset only when RTCSEL holds another clock (it
     * cannot be changed otherwise): after a power-up it is 0 and written
     * as it is, so the sketch's backup words written before the RTC's
     * first start survive it. The reset takes LSEON with it: LSE is turned
     * on after it. */
    if ((RCC_BDCTLR & BDCTLR_RTCSEL) != 0u && (RCC_BDCTLR & BDCTLR_RTCSEL) != sel) {
        RCC_BDCTLR |= BDCTLR_BDRST;
        RCC_BDCTLR &= ~BDCTLR_BDRST;
    }
    /* Each field read back before the next write: on CH32H417 RTCSEL written
     * right after the reset was lost (RTCEN alone stuck, the RTC had no clock;
     * its EVT waits ten NOPs between the two, 2026-11-14). */
    for (uint32_t spin = 0; (RCC_BDCTLR & BDCTLR_RTCSEL) != sel && spin < 1000u; spin++) {
        RCC_BDCTLR = (RCC_BDCTLR & ~BDCTLR_RTCSEL) | sel;
    }
    RCC_BDCTLR |= BDCTLR_RTCEN;                /* a write of its own, or it does not stick */
    for (uint32_t spin = 0; !(RCC_BDCTLR & BDCTLR_RTCEN) && spin < 1000u; spin++) {
        RCC_BDCTLR |= BDCTLR_RTCEN;
    }
}
#define rtc_clock_lsi() rtc_clock(BDCTLR_LSI)
#endif

#ifdef CH32RV_RTC_LSE
/* The crystal on and running: LSERDY within LSE_START_MS. Left off when it
 * does not start, and the RTC counts LSI instead. */
static int lse_start(void)
{
    if (RCC_BDCTLR & BDCTLR_LSERDY) {
        return 1;
    }
    RCC_BDCTLR |= BDCTLR_LSEON;
    const uint32_t t0 = millis();
    while (!(RCC_BDCTLR & BDCTLR_LSERDY)) {
        if (millis() - t0 >= LSE_START_MS) {
            RCC_BDCTLR &= ~BDCTLR_LSEON;
            return 0;
        }
    }
    return 1;
}
#endif

int ch32rv_rtc_begin(void)
{
    if (rtc_up) {
        return 1;
    }
    ch32rv_rtc_domain_open();
#ifdef CH32RV_RTC_LSE
    /* The crystal: running since an earlier boot (the backup domain keeps
     * LSE and the RTC across resets and on VBAT), or started now. */
    if (rtc_on(BDCTLR_LSE) && (RCC_BDCTLR & BDCTLR_LSERDY) && BKP_DR(1) == BKP_MARKER) {
        rtc_psc = BKP_DR(2) | ((uint32_t)BKP_DR(3) << 16);
        ch32rv_rtc_resync();
        rtc_lse = 1;
        rtc_up = 1;
        return 1;
    }
    if (lse_start()) {
        rtc_clock(BDCTLR_LSE);
        /* the reset rtc_clock() may have made took LSEON: on again */
        if (lse_start()) {
            rtc_lse = 1;
            rtc_psc = 32767u;
        }
    }
    if (!rtc_lse)
#endif
    {
        CH32RV_RCC_RSTSCKR |= 1u;              /* LSION: not kept by a reset */
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
    }
    ch32rv_rtc_resync();
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

int ch32rv_rtc_on_lse(void)
{
    return rtc_lse;
}

/* The RTC's own calibration (BKP_OCTLR on the families with a BKP block:
 * CAL takes `cal` of every 2^20 RTC clocks away, CCO puts out the RTC clock
 * / 64, ASOE / ASOS the alarm or the second pulse, all on the TAMPER pad;
 * X305 / X315 keep the three outputs in RTC_CTLRL, and no CAL; H41x has
 * neither). */
#if defined(CH32RV_RTC_X3X5)
#define RTC_OUT_REG     RTC_CTLRL32
#define RTC_OUT_SHIFT   8u
#elif !defined(CH32RV_RTC_STORE_RAM)
#define BKP_OCTLR       CH32RV_REG16(0x40006C2Cu)
#define RTC_OUT_REG     BKP_OCTLR
#define RTC_OUT_SHIFT   7u
/* CH32V103's CAL reads back as written and slows nothing (127 over three 40 s
 * windows against the crystal, 2026-11-18; V203 slows by the manual's
 * 0.95 ppm a unit): the prescaler alone there. */
#if !defined(CH32RV_SERIES_CH32V103)
#define RTC_HAS_CAL     1
#endif
#endif

int ch32rv_rtc_calibrate(int32_t ppm)
{
    if (!ch32rv_rtc_begin() || !rtc_lse || ppm > 500000 || ppm < -500000) {
        return 0;
    }
    /* A prescaler a step over 32767 slows the RTC by 10^6 / 32768 ppm; CAL
     * slows it by 10^6 / 2^20 ppm a unit, up to 127. The prescaler takes
     * what CAL cannot (all of it where there is no CAL), CAL the rest. */
    const int64_t micro = (int64_t)ppm * 32768;          /* in 10^-6 prescaler steps */
    int32_t steps;
    uint32_t cal = 0;
#ifdef RTC_HAS_CAL
    if (ppm >= 0) {
        const int64_t over = micro - (int64_t)127 * 1000000 / 32;   /* CAL's 127 units, in the same scale */
        steps = over > 0 ? (int32_t)((over + 999999) / 1000000) : 0;
    } else {
        steps = -(int32_t)((-micro + 999999) / 1000000);
    }
    /* what is left, in CAL units: (ppm - steps x 10^6 / 32768) x 2^20 / 10^6 */
    const int64_t left = ((int64_t)ppm * 32768 - (int64_t)steps * 1000000) * 32;
    cal = (uint32_t)((left + 500000) / 1000000);
    if (cal > 127u) {
        cal = 127u;
    }
#else
    steps = (int32_t)((micro >= 0 ? micro + 500000 : micro - 500000) / 1000000);
#endif
    ch32rv_rtc_domain_open();
    const uint32_t psc = (uint32_t)(32767 + steps);
    if (psc != rtc_psc) {
        /* Writing the prescaler starts the divider over without a count: the
         * part of the second gone so far would be lost. Written just after
         * the counter moves, that part is a few LSE periods (up to 1 s of
         * waiting here). */
        const uint32_t c0 = RTC(0x1C);
        const uint32_t t0 = millis();
        while (RTC(0x1C) == c0 && millis() - t0 < 1100u) {
        }
        const uint32_t irq = ch32rv_irq_save();
        rtc_psc = psc;
        config_begin();
        RTC(0x08) = (uint16_t)(rtc_psc >> 16);
        RTC(0x0C) = (uint16_t)rtc_psc;
        config_end();
        ch32rv_irq_restore(irq);
        BKP_DR(2) = (uint16_t)rtc_psc;
        BKP_DR(3) = (uint16_t)(rtc_psc >> 16);
    }
#ifdef RTC_HAS_CAL
    BKP_OCTLR = (uint16_t)((BKP_OCTLR & ~0x7Fu) | cal);
#else
    (void)cal;
#endif
    ch32rv_rtc_domain_seal();
    return 1;
}

int ch32rv_rtc_output(int mode)
{
#ifdef RTC_OUT_REG
    if (mode < 0 || mode > 3 || !ch32rv_rtc_begin()) {
        return 0;
    }
    ch32rv_rtc_domain_open();
    /* bits from RTC_OUT_SHIFT: CCO, ASOE, ASOS */
    static const uint32_t bits[4] = {0u, 1u, 2u, 2u | 4u};
    RTC_OUT_REG = (RTC_OUT_REG & ~(7u << RTC_OUT_SHIFT)) | (bits[mode] << RTC_OUT_SHIFT);
    ch32rv_rtc_domain_seal();
    return 1;
#else
    (void)mode;
    return 0;
#endif
}

uint64_t ch32rv_rtc_lse_ticks(void)
{
    if (!rtc_lse) {
        return 0;
    }
    uint32_t s, d;
    do {
        s = ((uint32_t)RTC(0x18) << 16) | RTC(0x1C);
        d = read_div();
    } while (s != (((uint32_t)RTC(0x18) << 16) | RTC(0x1C)));
    return (uint64_t)s * (rtc_psc + 1u) + (rtc_psc - (d > rtc_psc ? rtc_psc : d));
}

void ch32rv_rtc_raw_us(uint32_t *sec, uint32_t *us)
{
    uint32_t s, d;
    do {
        s = ((uint32_t)RTC(0x18) << 16) | RTC(0x1C);
        d = read_div();
    } while (s != (((uint32_t)RTC(0x18) << 16) | RTC(0x1C)));
    *sec = s;
    *us = (uint32_t)((uint64_t)(rtc_psc - (d > rtc_psc ? rtc_psc : d)) * 1000000u / (rtc_psc + 1u));
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
     * 2 s sleep took 3), so the register holds one less - V103 too: its flag
     * rose ~1 s after ALR was written equal to the counter, ~2 s for one
     * ahead (2026-11-18, read straight from the registers); written as is, a
     * 2 s alarm came after 2.6 - 2.9 s. It had been left out (2026-11-15) for
     * a hang that was its WFI not ending on SysTick (ch32rv_power.c). */
    sec--;
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

int ch32rv_rtc_on_lse(void)
{
    return 0;
}

uint64_t ch32rv_rtc_lse_ticks(void)
{
    return 0;
}

int ch32rv_rtc_calibrate(int32_t ppm)
{
    (void)ppm;
    return 0;
}

int ch32rv_rtc_output(int mode)
{
    (void)mode;
    return 0;
}

/* No RTC: the counter stays at 0 (the declarations are the same everywhere, so a sketch that reads it still links). */
void ch32rv_rtc_raw(uint32_t *sec, uint32_t *ms)
{
    *sec = 0;
    *ms = 0;
}

void ch32rv_rtc_raw_us(uint32_t *sec, uint32_t *us)
{
    *sec = 0;
    *us = 0;
}

#endif
