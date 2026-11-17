/* Deep sleep that keeps RAM (ch32rv_power.h). Measured 2026-10-28 on V003,
 * X035 and V307: docs/peripheral-a-design.ja.md 5.1. */
#include "Arduino.h"
#include <stdbool.h>
#include "internal/ch32rv_power.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_rtc.h"
#include "internal/ch32rv_interrupt.h"
#include "internal/ch32rv_exti_line.h"

volatile uint32_t ch32rv_time_domain;
volatile uint8_t ch32rv_wake_cause;

struct ch32rv_power_wake ch32rv_power_wake;

/* deepSleep()'s cause across its restart: RAM keeps power through it.
 * volatile: to the compiler the record starts as zero like any static, and
 * clang evaluates a constructor at compile time when it can - it read magic
 * as 0, dropped ch32rv_power_boot() and with it every store to the record
 * (V003, 2026-11-12: wakeupCause() was 0 after a timer deepSleep). A volatile
 * read it cannot evaluate. */
static volatile struct {
    uint32_t magic;
    uint32_t cause;
} wake_record __attribute__((section(".noinit")));
#define WAKE_MAGIC 0x51EE9C0Du

#if defined(CH32RV_SERIES_CH32V003) || defined(CH32RV_SERIES_CH32V002) || defined(CH32RV_SERIES_CH32V004) || \
    defined(CH32RV_SERIES_CH32V005) || defined(CH32RV_SERIES_CH32V006) || defined(CH32RV_SERIES_CH32V007) || \
    defined(CH32RV_SERIES_CH32M007)
/* The AWU sits in PWR and counts LSI (128 kHz nominal; a V003 ran 13 % slow,
 * so it is measured). Its event is EXTI line 9. */
#define POWER_AWU 1
#define AWU_CSR   CH32RV_REG32(0x40007008u)
#define AWU_WR    CH32RV_REG32(0x4000700Cu)
#define AWU_PSC   CH32RV_REG32(0x40007010u)
#define AWU_LINE  9u
#define AWU_ON_LSI 1
#define AWU_NOMINAL_HZ 128000u
#elif defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
/* A block of its own on a clock EVT calls HSI / 1024 (X035 has no LSI). It
 * is not exactly that: sleeps came out 5 % long on an X035C8T6, so it is
 * measured like the LSI. EXTI line 27. */
#define POWER_AWU 1
#define AWU_CSR   CH32RV_REG32(0x40026400u)
#define AWU_WR    CH32RV_REG32(0x40026404u)
#define AWU_PSC   CH32RV_REG32(0x40026408u)
#define AWU_LINE  27u
#define AWU_ON_LSI 0
#define AWU_NOMINAL_HZ (48000000u / 1024u)
#elif defined(CH32RV_SERIES_CH32M030)
/* In PWR as on V00x, but with no prescaler register: LSI (340 kHz nominal)
 * divided by 51000, so one count is about 150 ms, and a 4-bit window of 1-16
 * counts (written less one; the reference manual's 2.3.5 and 2.4.3). EXTI
 * line 17. What is left under one count is slept in a plain WFI, where
 * SysTick runs. Standby keeps RAM and resumes here: "the only difference
 * from stop is the regulator's low-power mode" (2.3.4), and the EVT's
 * Standby_Mode returns from it. */
#define POWER_AWU 1
#define AWU_CSR   CH32RV_REG32(0x40007008u)
#define AWU_WR    CH32RV_REG32(0x4000700Cu)
#define AWU_LINE  17u
#define AWU_ON_LSI 1
#define AWU_NOMINAL_HZ 340000u
#define AWU_FIXED_DIV 51000u
#define AWU_WINDOW_MAX 16u
#endif

#ifndef AWU_WINDOW_MAX
#define AWU_WINDOW_MAX 63u
#endif

#if defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
#define POWER_H41X 1          /* stop and sleep only: no standby, a PWR_CSR of its own */
#else
#define POWER_H41X 0
#endif
#if defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315)
#define POWER_X3X5 1          /* stop and sleep only: no PDDS, PWR_CSR holds only PVDO */
#else
#define POWER_X3X5 0
#endif
/* No standby that wakes through a reset: deepSleep() is a stop and a restart. */
#define POWER_NO_STANDBY (POWER_H41X || POWER_X3X5)

#if !defined(POWER_AWU) && defined(CH32RV_HAS_RTC)
/* Stop (RAM kept, resumes) woken by the RTC alarm on EXTI line 17; standby
 * (RAM lost, wakes through a reset) for deepSleep(). V307 2026-10-28: the
 * PLL is gone on waking and SystemInit() brings it back. */
#define POWER_RTC 1
#define ALARM_LINE CH32RV_RTC_ALARM_LINE      /* 17; 20 on X305 / X315 */
#define PWR_CSR        CH32RV_REG32(0x40007004u)
#define PWR_CTLR_LPDS  (1u << 0)
#define PWR_CTLR_CWUF  (1u << 2)
#define PWR_CTLR_CSBF  (1u << 3)
#define PWR_CSR_WUF    (1u << 0)
#define PWR_CSR_SBF    (1u << 1)
#endif

#define PWR_CTLR       CH32RV_REG32(0x40007000u)
#define PWR_CTLR_PDDS  (1u << 1)
#define PFIC_SCTLR     CH32RV_PFIC_SCTLR
#define SCTLR_SLEEPDEEP (1u << 2)
#define PFIC_IPRR(i)   CH32RV_REG32(CH32RV_PFIC_BASE + 0x280u + 4u * (i))
#define AWU_IRQN       21u
#define AWU_CSR_AWUEN  (1u << 1)

void ch32rv_millis_advance(uint32_t ms);      /* wiring_time.c */
void ch32rv_clock_check(void);
/* wiring_interrupts.c, weakly: it comes in with a pin wakeup
 * (ch32rv_power_pin.c) or attachInterrupt(), and a timer-only sleep should
 * not drag the EXTI handlers in. */
extern volatile uint32_t ch32rv_exti_seen __attribute__((weak));
void ch32rv_serial_sleep_prepare(void) __attribute__((weak));   /* HardwareSerial.cpp */

#if POWER_AWU

static volatile uint32_t awu_events;

CH32RV_INTERRUPT void AWU_IRQHandler(void)
{
    CH32RV_EXTI_INTFR = 1u << AWU_LINE;
    awu_events++;
}

/* Prescaler field -> divider (field 1 is reserved). */
static const uint16_t awu_div[16] = {1, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024,
                                     2048, 4096, 10240, 61440};
static uint32_t awu_hz;

static void awu_run(uint8_t field, uint8_t window)
{
    AWU_CSR &= ~AWU_CSR_AWUEN;
#ifdef AWU_FIXED_DIV
    (void)field;
    AWU_WR = (uint32_t)window - 1u;            /* the window is the value plus one */
#else
    AWU_PSC = field;
    AWU_WR = window;
#endif
    AWU_CSR |= AWU_CSR_AWUEN;                  /* the count starts again from 0 */
}

static void awu_lines(void)
{
    CH32RV_EXTI_FTENR |= 1u << AWU_LINE;       /* one edge per period: the pulse has two */
    CH32RV_EXTI_RTENR &= ~(1u << AWU_LINE);
    CH32RV_EXTI_INTENR |= 1u << AWU_LINE;
    CH32RV_EXTI_INTFR = 1u << AWU_LINE;
}

/* The calibration's window: periods of 16 x window ticks, about 8 ms at the
 * nominal clock - 63 on the LSI's 128 kHz, 23 on X035's 46.9 kHz. With 63
 * there a period was 21.5 ms, and the first lightSleep() of a boot slept
 * about 100 ms more than asked while it measured (200 ms took 317, CH32X035,
 * 2026-11-05). */
#ifdef AWU_FIXED_DIV
/* M030: one count (about 150 ms) is the shortest period, and one period after
 * the first event is what is timed: the first sleep of a boot is up to about
 * 300 ms longer while it measures. */
#define AWU_CAL_WINDOW 1u
#define AWU_CAL_PERIODS 1
#define AWU_CAL_TIMEOUT_US 400000u
#else
#define AWU_CAL_WINDOW (AWU_NOMINAL_HZ / 2000u < 63u ? AWU_NOMINAL_HZ / 2000u : 63u)
#define AWU_CAL_PERIODS 4
#define AWU_CAL_TIMEOUT_US 50000u
#endif

/* The AWU's clock, once per boot: four periods of 16 x AWU_CAL_WINDOW ticks
 * (about 8 ms each) timed with micros(), between consecutive events, so the
 * unknown phase of the first one does not count. */
static uint32_t awu_clock(void)
{
    ch32rv_clock_enable(PWR);                  /* or the AWU's registers ignore the writes */
#if AWU_ON_LSI
    CH32RV_RCC_RSTSCKR |= 1u;                  /* LSION */
    for (uint32_t spin = 0; !(CH32RV_RCC_RSTSCKR & 2u) && spin < 1000000u; spin++) {
    }
#endif
    ch32rv_irq_disable(AWU_IRQN);
    awu_lines();
    awu_run(5, AWU_CAL_WINDOW);
    uint32_t t0 = 0;
    for (int n = 0; n <= AWU_CAL_PERIODS; n++) {
        const uint32_t start = micros();
        while (!(CH32RV_EXTI_INTFR & (1u << AWU_LINE))) {
            if (micros() - start > AWU_CAL_TIMEOUT_US) {
                AWU_CSR &= ~AWU_CSR_AWUEN;
                return AWU_NOMINAL_HZ;         /* no event: keep the nominal value */
            }
        }
        CH32RV_EXTI_INTFR = 1u << AWU_LINE;
        if (n == 0) {
            t0 = micros();
        }
    }
    const uint32_t us = micros() - t0;
    AWU_CSR &= ~AWU_CSR_AWUEN;
#ifdef AWU_FIXED_DIV
    /* 51000 ticks x 10^6 / us, split to stay in 32 bits: 51000 x 15625 is
     * 7.97e8, and x 64 after the divide (a resolution of 64 Hz in 340 kHz). */
    return AWU_FIXED_DIV * 15625u / us * 64u;
#else
    return 4u * 16u * AWU_CAL_WINDOW * 1000000u / us;     /* at most 4.03e9: still 32 bits */
#endif
}

/* AWU ticks to microseconds, in 32 bits (V003 has no 64-bit divide, and the
 * library one is 2.4 KB): ticks <= 63 x 61440, so ticks x 1000 still fits. */
static uint32_t awu_us(uint32_t ticks)
{
    const uint32_t ms = ticks * 1000u / awu_hz;
    const uint32_t rest = ticks * 1000u % awu_hz;
    return ms * 1000u + rest * 1000u / awu_hz;
}

/* The AWU period closest to (not above) us, and what it really is. The
 * ticks are us x hz / 10^6, split so that nothing overflows 32 bits: whole
 * milliseconds times hz / 8 over 125, plus the rest. */
static uint32_t awu_period(uint32_t us, uint8_t *field, uint8_t *window)
{
    const uint32_t hz8 = awu_hz >> 3;
    const uint32_t ticks = us / 1000u * hz8 / 125u + us % 1000u * hz8 / 125000u;
#ifdef AWU_FIXED_DIV
    /* 0: shorter than one count, which this AWU cannot do. */
    uint32_t w = ticks / AWU_FIXED_DIV;
    if (w == 0u) {
        return 0;
    }
    if (w > AWU_WINDOW_MAX) {
        w = AWU_WINDOW_MAX;
    }
    *field = 0;
    *window = (uint8_t)w;
    return awu_us(w * AWU_FIXED_DIV);
#endif
    for (uint8_t f = 0; f < 16; f++) {
        if (f == 1) {
            continue;
        }
        uint32_t w = ticks / awu_div[f];
        if (w <= 63u) {
            if (w == 0u) {
                w = 1u;
            }
            *field = f;
            *window = (uint8_t)w;
            return awu_us(w * awu_div[f]);
        }
    }
    *field = 15;
    *window = 63;
    return awu_us(63u * 61440u);
}

/* The longest AWU period. */
static uint32_t awu_longest_us(void)
{
#ifdef AWU_FIXED_DIV
    return awu_us(AWU_WINDOW_MAX * AWU_FIXED_DIV);
#else
    return awu_us(63u * 61440u);
#endif
}

#endif

int ch32rv_power_supported(void)
{
#if defined(CH32RV_SERIES_CH32V103)
    /* V103 does not come back from lightSleep()'s stop: with the alarm
     * written as V103 counts it (2026-11-15) its alarm callback, setAlarm()
     * and deepSleep()'s wake-up work, but the first lightSleep(200 ms) still
     * never returns, the probe cannot attach while it sleeps and NRST does not
     * bring it back (it came back by itself minutes later). Unsupported until
     * that is found (TODO). */
    return 0;
#elif POWER_AWU || POWER_RTC
    return 1;
#else
    return 0;
#endif
}

uint32_t ch32rv_power_awu_max_us(void)
{
#if POWER_AWU
    if (!awu_hz) {
        awu_hz = awu_clock();
    }
    return awu_longest_us();
#else
    return 0;
#endif
}

/* One deep sleep: until any interrupt. The interrupt's handler has run when
 * this returns. */
static void deep_wfi(void)
{
#if POWER_RTC
    /* Stop: PDDS clear. The regulator in its low-power mode (LPDS) on V20x /
     * V30x. On L103 / M103 and V205 LPDS is their "stop mode 3", which wants
     * FLASH_LP preset first (the reference manuals): entered without it the
     * part never came back from a 1.5 s sleep (L103, 2026-11-12), so they
     * stop in mode 1 (LPDS clear) - mode 3's lower current is a TODO. */
#if defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103) || defined(CH32RV_SERIES_CH32V205)
    PWR_CTLR &= ~(PWR_CTLR_PDDS | PWR_CTLR_LPDS);
#elif POWER_NO_STANDBY
    /* X305 / X315: no PDDS either; LPDS left clear (its EVT lowers VDDK
     * with PLAN first - a TODO, as on H41x).
     * H41x: no PDDS (its bit is reserved) and the regulator kept in normal
     * mode: LPDS wants SYSCLK on HSI and VDDK lowered first (the EVT's
     * Stop_Mode_RTC_WakeUp). Stop takes effect only with both cores in it:
     * the V3F waits for work in it (crt0_ch32.S). */
    PWR_CTLR &= ~PWR_CTLR_LPDS;
#else
    PWR_CTLR = (PWR_CTLR & ~PWR_CTLR_PDDS) | PWR_CTLR_LPDS;
#endif
    PFIC_SCTLR |= SCTLR_SLEEPDEEP;
    __asm__ volatile ("wfi");
    PFIC_SCTLR &= ~SCTLR_SLEEPDEEP;
    PWR_CTLR &= ~PWR_CTLR_LPDS;
#else
    PWR_CTLR |= PWR_CTLR_PDDS;                 /* the RAM-keeping deep mode on V00x / X035 */
    PFIC_SCTLR |= SCTLR_SLEEPDEEP;
    __asm__ volatile ("wfi");
    PFIC_SCTLR &= ~SCTLR_SLEEPDEEP;
    PWR_CTLR &= ~PWR_CTLR_PDDS;
#endif
}

#if POWER_RTC
/* The alarm's interrupt is ch32rv_rtc_alarm.c's: one register for the sleeps
 * and the sketch's alarm. */
#define alarm_events ch32rv_rtc_alarm_events

/* (s, ms) + add ms. */
static void rtc_add(uint32_t *s, uint32_t *ms, uint32_t add)
{
    *s += add / 1000u;
    *ms += add % 1000u;
    if (*ms >= 1000u) {
        *ms -= 1000u;
        (*s)++;
    }
}

/* b - a in milliseconds, b not before a (sleeps are far shorter than 49 days). */
static uint32_t rtc_diff(uint32_t as, uint32_t ams, uint32_t bs, uint32_t bms)
{
    return (bs - as) * 1000u + bms - ams;
}

static int rtc_before(uint32_t as, uint32_t ams, uint32_t bs, uint32_t bms)
{
    return as < bs || (as == bs && ams < bms);
}

/* Milliseconds from now until the counter reaches the sketch's alarm (0: it
 * has; at most 2^32 - 1). The RTC started first: the alarm did that already. */
static uint32_t alarm_ms(void)
{
    const uint32_t c = ch32rv_rtc_counter_for(ch32rv_rtc_user_alarm_at());
    uint32_t s, m;
    ch32rv_rtc_raw(&s, &m);
    if (c <= s) {
        return 0;
    }
    const uint32_t secs = c - s;
    if (secs > 4294966u) {
        return 0xFFFFFFFFu;
    }
    return secs * 1000u - m;
}
#endif

int ch32rv_power_light_sleep(uint32_t timer_ms, uint32_t pin_lines, uint32_t wdt_slice_us,
                           uint32_t *slept_ms)
{
    /* Everything in 32 bits: whole milliseconds, and the microseconds under
     * one carried to the next sleep. */
    uint32_t known_ms = 0;
    static uint32_t carry_us;
    int woke = 0;
#if POWER_AWU
    if (!timer_ms && !pin_lines) {
        if (slept_ms) {
            *slept_ms = 0;
        }
        return 0;
    }
    if (!awu_hz) {
        awu_hz = awu_clock();
    }
    if (ch32rv_serial_sleep_prepare) {
        ch32rv_serial_sleep_prepare();
    }
    ch32rv_clock_enable(PWR);
    awu_lines();
    PFIC_IPRR(AWU_IRQN >> 5) = 1u << (AWU_IRQN & 31u);
    ch32rv_irq_enable(AWU_IRQN);
    if (!&ch32rv_exti_seen) {
        pin_lines = 0;
    }
    if (pin_lines) {
        ch32rv_exti_seen &= ~pin_lines;
    }
    const uint32_t awu_max = awu_longest_us();
    uint32_t awake_ms = 0;                      /* slept in a plain WFI: millis() counted it */

    for (;;) {
        /* This period: what is left of the timer, no longer than the IWDG
         * may wait or one AWU period. No timer and no IWDG: no period at
         * all, only the pin. */
        uint32_t want = 0;
        if (timer_ms) {
            const uint32_t left_ms = timer_ms - known_ms;
            want = left_ms > awu_max / 1000u ? awu_max : left_ms * 1000u;
        }
        if (wdt_slice_us && (!want || want > wdt_slice_us)) {
            want = wdt_slice_us;
        }
        if (want > awu_max) {
            want = awu_max;
        }
        uint32_t period = 0;
        if (want) {
            uint8_t field, window;
            period = awu_period(want, &field, &window);
            if (!period) {
                /* Under the AWU's shortest period (M030): a plain WFI, woken by
                 * SysTick every millisecond, until the rest has passed. */
                const uint32_t t0 = (uint32_t)millis();
                const uint32_t rest = want / 1000u;
                while ((uint32_t)millis() - t0 < rest && !(pin_lines && (ch32rv_exti_seen & pin_lines))) {
                    __asm__ volatile ("wfi");
                }
                const uint32_t took = (uint32_t)millis() - t0;
                known_ms += took;
                awake_ms += took;
                if (pin_lines && (ch32rv_exti_seen & pin_lines)) {
                    woke = CH32RV_POWER_WOKE_PIN;
                    break;
                }
                if (!timer_ms || known_ms + 1u >= timer_ms) {
                    woke = CH32RV_POWER_WOKE_TIMER;
                    break;
                }
                continue;
            }
            awu_run(field, window);
        }
        const uint32_t events = awu_events;
        while (awu_events == events && !(pin_lines && (ch32rv_exti_seen & pin_lines))) {
            deep_wfi();
        }
        AWU_CSR &= ~AWU_CSR_AWUEN;
        ch32rv_clock_check();
        if (pin_lines && (ch32rv_exti_seen & pin_lines)) {
            woke = CH32RV_POWER_WOKE_PIN;
            break;
        }
        carry_us += period;
        known_ms += carry_us / 1000u;
        carry_us %= 1000u;
#ifdef CH32RV_IWDG_BASE
        if (wdt_slice_us) {
            CH32RV_IWDG_CTLR = CH32RV_IWDG_KEY_FEED;
        }
#endif
        /* A period is never longer than what is left (awu_period), so the
         * rest is slept in shorter ones until under a millisecond remains. */
        if (timer_ms && known_ms + 1u >= timer_ms) {
            woke = CH32RV_POWER_WOKE_TIMER;
            break;
        }
    }
    ch32rv_irq_disable(AWU_IRQN);
    CH32RV_EXTI_INTENR &= ~(1u << AWU_LINE);
    ch32rv_millis_advance(known_ms - awake_ms);
    if (woke == CH32RV_POWER_WOKE_PIN) {
        ch32rv_time_domain++;                  /* part of a period went by unmeasured */
    }
#elif POWER_RTC
    /* The RTC times everything, a pin wake included, so millis() is always
     * advanced by what really passed and timeDomain() never moves. Whole
     * seconds are slept in stop until the alarm; what is left under a second
     * in a plain WFI, where SysTick runs and millis() counts by itself. */
    (void)carry_us;
    if (!timer_ms && !pin_lines) {
        if (slept_ms) {
            *slept_ms = 0;
        }
        return 0;
    }
    ch32rv_rtc_begin();
    if (ch32rv_serial_sleep_prepare) {
        ch32rv_serial_sleep_prepare();
    }
    if (!&ch32rv_exti_seen) {
        pin_lines = 0;
    }
    if (pin_lines) {
        ch32rv_exti_seen &= ~pin_lines;
    }
    CH32RV_EXTI_RTENR |= 1u << ALARM_LINE;
    CH32RV_EXTI_INTENR |= 1u << ALARM_LINE;
    CH32RV_EXTI_INTFR = 1u << ALARM_LINE;
    PFIC_IPRR(CH32RV_IRQN_RTCAlarm >> 5) = 1u << (CH32RV_IRQN_RTCAlarm & 31u);
    ch32rv_rtc_alarm_in_sleep = 1;
    ch32rv_irq_enable(CH32RV_IRQN_RTCAlarm);

    uint32_t s0, m0, end_s = 0, end_ms = 0;
    ch32rv_rtc_raw(&s0, &m0);
    if (timer_ms) {
        end_s = s0;
        end_ms = m0;
        rtc_add(&end_s, &end_ms, timer_ms);
    }
    const uint32_t start_millis = (uint32_t)millis();
    for (;;) {
        uint32_t s, m;
        ch32rv_rtc_raw(&s, &m);
        if (timer_ms && !rtc_before(s, m, end_s, end_ms)) {
            woke = CH32RV_POWER_WOKE_TIMER;
            break;
        }
        /* The next stop ends on a whole second: the timer's last whole
         * second, or half the IWDG's time on, whichever comes first. */
        /* Second 0 is a real time: an RTC started a moment ago (the first
         * use since power-up, here) counts from 0, and a 200 ms sleep ends in
         * second 0. With 0 meaning "no alarm" it went to stop with none set
         * and never woke (V203 / V208 / V307 / L103 / V205, 2026-11-12). */
        uint32_t alarm = 0;
        bool have_alarm = false;
        if (timer_ms) {
            alarm = end_s;
            have_alarm = true;
        }
        if (wdt_slice_us) {
            const uint32_t w = s + (wdt_slice_us / 1000000u ? wdt_slice_us / 1000000u : 1u);
            if (!have_alarm || w < alarm) {
                alarm = w;
                have_alarm = true;
            }
        }
        if (have_alarm && alarm <= s) {
            /* Under a second to go: a plain WFI, SysTick wakes it each ms. */
            __asm__ volatile ("wfi");
        } else {
            if (have_alarm) {
                ch32rv_rtc_alarm_arm(alarm);
            } else {
                ch32rv_rtc_alarm_restore();     /* only the sketch's alarm, if any */
            }
            const uint32_t events = alarm_events;
            uint32_t ss, sm;
            ch32rv_rtc_raw(&ss, &sm);
            while (alarm_events == events && !(pin_lines && (ch32rv_exti_seen & pin_lines))) {
                deep_wfi();
            }
            ch32rv_clock_check();
            ch32rv_rtc_resync();
            uint32_t es, em;
            ch32rv_rtc_raw(&es, &em);
            ch32rv_millis_advance(rtc_diff(ss, sm, es, em));
        }
#ifdef CH32RV_IWDG_BASE
        if (wdt_slice_us) {
            CH32RV_IWDG_CTLR = CH32RV_IWDG_KEY_FEED;
        }
#endif
        if (pin_lines && (ch32rv_exti_seen & pin_lines)) {
            woke = CH32RV_POWER_WOKE_PIN;
            break;
        }
    }
    ch32rv_rtc_alarm_lines_off();               /* kept on for attachExtiLine() */
    ch32rv_rtc_alarm_in_sleep = 0;
    ch32rv_rtc_alarm_restore();                 /* the sketch's alarm back in the register */
    known_ms = (uint32_t)millis() - start_millis;
#else
    (void)timer_ms;
    (void)pin_lines;
    (void)wdt_slice_us;
    (void)carry_us;
#endif
    if (slept_ms) {
        *slept_ms = known_ms;
    }
    return woke;
}

int ch32rv_power_set_timer(uint64_t us)
{
    if (!ch32rv_power_supported()) {
        return 0;
    }
    /* To milliseconds without a 64-bit divide: below 2^32 us exactly, above
     * it from us / 1024 x 1.024 (to a millisecond in 71 minutes), at most
     * 2^32 - 1 ms (49 days). */
    uint32_t ms;
    if (!(us >> 32)) {
        ms = (uint32_t)us / 1000u;
    } else if (us >> 42) {
        ms = 0xFFFFFFFFu;
    } else {
        const uint32_t k = (uint32_t)(us >> 10);
        ms = k + k / 125u * 3u;
    }
    ch32rv_power_wake.timer_ms = ms ? ms : (us ? 1u : 0u);
    return 1;
}

void ch32rv_power_clear(void)
{
    ch32rv_power_wake.timer_ms = 0;
    ch32rv_power_wake.pins = 0;
    ch32rv_power_wake.alarm = 0;
    ch32rv_power_wake.lines = 0;
}

int ch32rv_power_set_alarm(void)
{
#if POWER_RTC
    ch32rv_power_wake.alarm = 1;
    return 1;
#else
    return 0;
#endif
}

int ch32rv_power_sleep(uint32_t wdt_slice_us)
{
    if (!ch32rv_power_supported()) {
        return 2;
    }
    const struct ch32rv_power_wake *w = &ch32rv_power_wake;
    uint32_t timer_ms = w->timer_ms;
    int alarm_first = 0;
#if POWER_RTC
    /* enableAlarmWakeup(): the sketch's alarm ends the sleep as a timer to
     * it would, and wakeupCause() says which came first. */
    if (w->alarm && ch32rv_rtc_user_alarm_at()) {
        const uint32_t to = alarm_ms();
        if (!to) {
            ch32rv_wake_cause = CH32RV_POWER_WOKE_ALARM;     /* already there */
            return 0;
        }
        if (!timer_ms || to <= timer_ms) {
            timer_ms = to;
            alarm_first = 1;
        }
    }
#endif
    if (!timer_ms && !w->pins && !w->lines) {
        return 1;
    }
    uint32_t lines = w->lines;
    for (uint8_t i = 0; i < w->pins; i++) {
        /* Woken by a level, as ESP32's ext0: one already there is a wake. */
        if (digitalRead(w->pin[i]) == w->level[i]) {
            ch32rv_wake_cause = CH32RV_POWER_WOKE_PIN;
            return 0;
        }
        lines |= 1u << CH32RV_PIN_BIT(CH32RV_PIN_RESOLVE(w->pin[i]));
    }
    int woke = ch32rv_power_light_sleep(timer_ms, lines, wdt_slice_us, 0);
    if (!woke) {
        woke = CH32RV_POWER_WOKE_TIMER;
    }
    /* w->lines is set only with ch32rv_exti_line.c, which brings the flags. */
    if (woke == CH32RV_POWER_WOKE_PIN && w->lines && (ch32rv_exti_seen & w->lines)) {
        woke = CH32RV_POWER_WOKE_EXTI;
    }
    if (woke == CH32RV_POWER_WOKE_TIMER && alarm_first) {
        woke = CH32RV_POWER_WOKE_ALARM;
    }
    ch32rv_wake_cause = (uint8_t)woke;
    return 0;
}

void ch32rv_power_keep_cause(void)
{
    wake_record.cause = ch32rv_wake_cause;
    wake_record.magic = WAKE_MAGIC;
}

/* Before setup() (.init_array): linked only with the rest of this file, so a
 * sketch that never sleeps pays nothing for it. */
__attribute__((constructor)) void ch32rv_power_boot(void)
{
    if (wake_record.magic == WAKE_MAGIC) {
        ch32rv_wake_cause = (uint8_t)wake_record.cause;
    }
    wake_record.magic = 0;
#if POWER_RTC && !POWER_NO_STANDBY
    /* Out of standby (RAM was lost, so this is the only trace): it was
     * entered for a timer only (ch32rv_power_standby()). The PWR clock
     * first, or the flags read 0. */
    ch32rv_clock_enable(PWR);
    /* SBF outlives a reset that is not a wake (reflashing pulled the chip
     * out of standby once), so only with the reset flags a wake leaves: the
     * power-on flag alone (V307 2026-10-28). */
    const uint32_t flags = CH32RV_RCC_RSTSCKR & 0xFE000000u;
    /* SBF on V307; a V203 wakes with only WUF (2026-11-13), which is also
     * what its EVT's Standby_Mode checks. A real power-on clears both. */
    /* V103 wakes with no reset flag at all (2026-11-15: resetReason() 0 after
     * an alarm's standby); nothing else leaves none. */
    if ((PWR_CSR & (PWR_CSR_SBF | PWR_CSR_WUF)) && (flags == CH32RV_RST_POR || flags == 0u)) {
        ch32rv_wake_cause = ch32rv_rtc_take_alarm_standby() ? CH32RV_POWER_WOKE_ALARM : CH32RV_POWER_WOKE_TIMER;
        PWR_CTLR |= PWR_CTLR_CSBF | PWR_CTLR_CWUF;
    }
#endif
}

int ch32rv_power_can_standby(void)
{
#if POWER_RTC && !POWER_NO_STANDBY
    const int alarm = ch32rv_power_wake.alarm && ch32rv_rtc_user_alarm_at();
    return (ch32rv_power_wake.timer_ms || alarm) && !ch32rv_power_wake.pins && !ch32rv_power_wake.lines;
#else
    return 0;
#endif
}

void ch32rv_power_standby(void)
{
#if POWER_RTC && !POWER_NO_STANDBY
    ch32rv_rtc_begin();
    if (ch32rv_serial_sleep_prepare) {
        ch32rv_serial_sleep_prepare();
    }
    uint32_t s, m;
    ch32rv_rtc_raw(&s, &m);
    rtc_add(&s, &m, ch32rv_power_wake.timer_ms);
    uint32_t end = s + (m ? 1u : 0u);          /* the alarm is whole seconds: round up */
    /* The sketch's alarm, when it wakes and comes first: the next boot says
     * so through the backup domain (RAM is lost). */
    int by_alarm = 0;
    if (ch32rv_power_wake.alarm && ch32rv_rtc_user_alarm_at()) {
        const uint32_t c = ch32rv_rtc_counter_for(ch32rv_rtc_user_alarm_at());
        if (!ch32rv_power_wake.timer_ms || c <= end) {
            end = c;
            by_alarm = 1;
        }
    }
    ch32rv_rtc_mark_alarm_standby(by_alarm);
    ch32rv_rtc_alarm(end);
    /* Only the alarm: standby loses RAM and every peripheral anyway, and a
     * line left on - a pin wakeup's falling edge (lightSleep() earlier) or an
     * attachInterrupt() - ended standby without the wake flags, so the next
     * boot read no cause (V203 2026-11-13, after a pin lightSleep). */
    CH32RV_EXTI_INTENR = 1u << ALARM_LINE;
    CH32RV_EXTI_EVENR = 0;
    CH32RV_EXTI_FTENR = 0;
    CH32RV_EXTI_RTENR = 1u << ALARM_LINE;     /* the alarm's rising edge ends standby */
    CH32RV_EXTI_INTFR = 0xFFFFFFFFu;
    PWR_CTLR |= PWR_CTLR_CWUF;
#if defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103)
    /* FLASH_LP [11:10] = 10 (flash low-power mode 1), as the EVT's
     * PWR_EnterSTANDBYMode() writes it. Left at its reset value 01 the RTC
     * alarm never ends standby: V205RCT6 2026-11-13, 1 s alarm, stuck until
     * NRST with 01 (with or without LPDS), woke with 10 (with or without). */
    PWR_CTLR = (PWR_CTLR & ~(3u << 10)) | (2u << 10);
#endif
    /* PDDS alone, as every EVT's PWR_EnterSTANDBYMode(): with LPDS set too a
     * V203C8T6 never woke on the alarm (2026-11-13; without it, 2 s later). */
    PWR_CTLR = (PWR_CTLR & ~PWR_CTLR_LPDS) | PWR_CTLR_PDDS;
    PFIC_SCTLR |= SCTLR_SLEEPDEEP;
    for (;;) {
        __asm__ volatile ("wfi");              /* wakes through a reset */
    }
#endif
}
