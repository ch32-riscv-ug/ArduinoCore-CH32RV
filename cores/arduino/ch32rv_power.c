/* Deep sleep that keeps RAM (ch32rv_power.h). Measured 2026-10-28 on V003,
 * X035 and V307: docs/peripheral-a-design.ja.md 5.1. */
#include "Arduino.h"
#include "ch32rv_power.h"
#include "ch32rv_registers.h"
#include "ch32rv_rtc.h"

volatile uint32_t ch32rv_time_domain;
volatile uint8_t ch32rv_wake_cause;

struct ch32rv_power_wake ch32rv_power_wake;

/* deepSleep()'s cause across its restart: RAM keeps power through it. */
static struct {
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
#endif

#if !defined(POWER_AWU) && defined(CH32RV_HAS_RTC)
/* Stop (RAM kept, resumes) woken by the RTC alarm on EXTI line 17; standby
 * (RAM lost, wakes through a reset) for deepSleep(). V307 2026-10-28: the
 * PLL is gone on waking and SystemInit() brings it back. */
#define POWER_RTC 1
#define ALARM_LINE 17u
#define PWR_CSR        CH32RV_REG32(0x40007004u)
#define PWR_CTLR_LPDS  (1u << 0)
#define PWR_CTLR_CWUF  (1u << 2)
#define PWR_CTLR_CSBF  (1u << 3)
#define PWR_CSR_SBF    (1u << 1)
#endif

#define PWR_CTLR       CH32RV_REG32(0x40007000u)
#define PWR_CTLR_PDDS  (1u << 1)
#define PFIC_SCTLR     CH32RV_REG32(0xE000ED10u)
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

__attribute__((interrupt)) void AWU_IRQHandler(void)
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
    AWU_PSC = field;
    AWU_WR = window;
    AWU_CSR |= AWU_CSR_AWUEN;                  /* the count starts again from 0 */
}

static void awu_lines(void)
{
    CH32RV_EXTI_FTENR |= 1u << AWU_LINE;       /* one edge per period: the pulse has two */
    CH32RV_EXTI_RTENR &= ~(1u << AWU_LINE);
    CH32RV_EXTI_INTENR |= 1u << AWU_LINE;
    CH32RV_EXTI_INTFR = 1u << AWU_LINE;
}

/* The AWU's clock, once per boot: four periods of 16 x 63 ticks (about 8 ms
 * each) timed with micros(), between consecutive events, so the unknown
 * phase of the first one does not count. */
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
    awu_run(5, 63);
    uint32_t t0 = 0;
    for (int n = 0; n <= 4; n++) {
        const uint32_t start = micros();
        while (!(CH32RV_EXTI_INTFR & (1u << AWU_LINE))) {
            if (micros() - start > 50000u) {
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
    return 4u * 16u * 63u * 1000000u / us;     /* 4.03e9: still 32 bits */
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

#endif

int ch32rv_power_supported(void)
{
#if POWER_AWU || POWER_RTC
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
    return awu_us(63u * 61440u);
#else
    return 0;
#endif
}

/* One deep sleep: until any interrupt. The interrupt's handler has run when
 * this returns. */
static void deep_wfi(void)
{
#if POWER_RTC
    /* Stop: PDDS clear, the regulator in its low-power mode. */
    PWR_CTLR = (PWR_CTLR & ~PWR_CTLR_PDDS) | PWR_CTLR_LPDS;
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
static volatile uint32_t alarm_events;

__attribute__((interrupt)) void RTCAlarm_IRQHandler(void)
{
    CH32RV_EXTI_INTFR = 1u << ALARM_LINE;
    CH32RV_REG16(0x40002804u) &= ~(1u << 1);   /* RTC CTLRL.ALRF */
    alarm_events++;
}

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
    const uint32_t awu_max = awu_us(63u * 61440u);

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
    ch32rv_millis_advance(known_ms);
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
        uint32_t alarm = 0;
        if (timer_ms) {
            alarm = end_s;
        }
        if (wdt_slice_us) {
            const uint32_t w = s + (wdt_slice_us / 1000000u ? wdt_slice_us / 1000000u : 1u);
            if (!alarm || w < alarm) {
                alarm = w;
            }
        }
        if (alarm && alarm <= s) {
            /* Under a second to go: a plain WFI, SysTick wakes it each ms. */
            __asm__ volatile ("wfi");
        } else {
            if (alarm) {
                ch32rv_rtc_alarm(alarm);
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
    ch32rv_irq_disable(CH32RV_IRQN_RTCAlarm);
    CH32RV_EXTI_INTENR &= ~(1u << ALARM_LINE);
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
}

int ch32rv_power_sleep(uint32_t wdt_slice_us)
{
    if (!ch32rv_power_supported()) {
        return 2;
    }
    const struct ch32rv_power_wake *w = &ch32rv_power_wake;
    if (!w->timer_ms && !w->pins) {
        return 1;
    }
    uint32_t lines = 0;
    for (uint8_t i = 0; i < w->pins; i++) {
        /* Woken by a level, as ESP32's ext0: one already there is a wake. */
        if (digitalRead(w->pin[i]) == w->level[i]) {
            ch32rv_wake_cause = CH32RV_POWER_WOKE_PIN;
            return 0;
        }
        lines |= 1u << CH32RV_PIN_BIT(CH32RV_PIN_RESOLVE(w->pin[i]));
    }
    const int woke = ch32rv_power_light_sleep(w->timer_ms, lines, wdt_slice_us, 0);
    ch32rv_wake_cause = (uint8_t)(woke ? woke : CH32RV_POWER_WOKE_TIMER);
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
#if POWER_RTC
    /* Out of standby (RAM was lost, so this is the only trace): it was
     * entered for a timer only (ch32rv_power_standby()). The PWR clock
     * first, or the flags read 0. */
    ch32rv_clock_enable(PWR);
    /* SBF outlives a reset that is not a wake (reflashing pulled the chip
     * out of standby once), so only with the reset flags a wake leaves: the
     * power-on flag alone (V307 2026-10-28). */
    const uint32_t flags = CH32RV_RCC_RSTSCKR & 0xFE000000u;
    if ((PWR_CSR & PWR_CSR_SBF) && flags == CH32RV_RST_POR) {
        ch32rv_wake_cause = CH32RV_POWER_WOKE_TIMER;
        PWR_CTLR |= PWR_CTLR_CSBF | PWR_CTLR_CWUF;
    }
#endif
}

int ch32rv_power_can_standby(void)
{
#if POWER_RTC
    return ch32rv_power_wake.timer_ms && !ch32rv_power_wake.pins;
#else
    return 0;
#endif
}

void ch32rv_power_standby(void)
{
#if POWER_RTC
    ch32rv_rtc_begin();
    if (ch32rv_serial_sleep_prepare) {
        ch32rv_serial_sleep_prepare();
    }
    uint32_t s, m;
    ch32rv_rtc_raw(&s, &m);
    rtc_add(&s, &m, ch32rv_power_wake.timer_ms);
    ch32rv_rtc_alarm(s + (m ? 1u : 0u));       /* the alarm is whole seconds: round up */
    CH32RV_EXTI_RTENR |= 1u << ALARM_LINE;     /* the alarm's rising edge ends standby */
    CH32RV_EXTI_INTENR |= 1u << ALARM_LINE;
    CH32RV_EXTI_INTFR = 1u << ALARM_LINE;
    PWR_CTLR |= PWR_CTLR_CWUF;
    PWR_CTLR |= PWR_CTLR_PDDS | PWR_CTLR_LPDS;
    PFIC_SCTLR |= SCTLR_SLEEPDEEP;
    for (;;) {
        __asm__ volatile ("wfi");              /* wakes through a reset */
    }
#endif
}
