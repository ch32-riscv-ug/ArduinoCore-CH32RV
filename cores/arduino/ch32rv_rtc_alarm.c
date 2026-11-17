/* The RTC alarm's interrupt, the sketch's alarm (CH32RV.setAlarm()) and the
 * sleeps' use of the same register (ch32rv_rtc.h). One alarm register: a sleep
 * arms it for whichever comes first, its end or the sketch's alarm, and puts
 * the sketch's back on waking. A file of its own, linked by a sleep or an
 * alarm, not by time(). */
#include "Arduino.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_rtc.h"
#include "internal/ch32rv_interrupt.h"
#include "internal/ch32rv_exti_line.h"

#ifdef CH32RV_HAS_RTC

#define RTC_CTLRL       CH32RV_REG16(0x40002804u)
#define RTC_CTLRL_ALRF  (1u << 1)
#define RTC_CTLRH       CH32RV_REG16(0x40002800u)
#define RTC_CTLRH_ALRIE (1u << 1)
#define PFIC_IPSR(i)    CH32RV_REG32(CH32RV_PFIC_BASE + 0x200u + 4u * (i))
#define PFIC_IPRR(i)    CH32RV_REG32(CH32RV_PFIC_BASE + 0x280u + 4u * (i))

volatile uint32_t ch32rv_rtc_alarm_events;
volatile uint8_t ch32rv_rtc_alarm_in_sleep;

static volatile uint32_t user_when;        /* wall seconds; 0: none */
static void (*volatile user_cb)(void);

/* attachExtiLine(CH32RV_EXTI_RTC_ALARM)'s callbacks (ch32rv_exti_line.c,
 * the only writer): the line's edges go there too, and while it is set the
 * line stays on when the alarm and the sleeps let it go. A sketch that never
 * attaches the line has no store to it, and the link-time optimiser drops
 * every test of it: the handler is the alarm's alone. */
void (*ch32rv_rtc_alarm_line_hook)(uint32_t pending);

static uint32_t wall_now(void)
{
    uint32_t s, m;
    ch32rv_rtc_get(&s, &m);
    return s;
}

CH32RV_INTERRUPT void RTCAlarm_IRQHandler(void)
{
    void (*const hook)(uint32_t) = ch32rv_rtc_alarm_line_hook;
    const uint32_t pending = hook ? CH32RV_EXTI_INTFR & (1u << CH32RV_RTC_ALARM_LINE) : 0u;
    CH32RV_EXTI_INTFR = 1u << CH32RV_RTC_ALARM_LINE;
    RTC_CTLRL &= ~RTC_CTLRL_ALRF;
    ch32rv_rtc_alarm_events++;
    const uint32_t when = user_when;
    int due = 0;
    if (when && ch32rv_rtc_alarm_in_sleep) {
        /* Out of a stop the RTC's registers are stale until RSF (the
         * reference manuals' "special read-write" rule): an alarm read against
         * the old count looked early, the sleep wrote the same alarm again
         * for a count already passed and never woke (CH32H417 2026-11-17,
         * setAlarm(+2 s) then lightSleep() with a 10 s timer). */
        ch32rv_rtc_resync();
    }
    if (when) {
        if (wall_now() >= when) {
            user_when = 0;                     /* once: setAlarm() again for the next */
            void (*const cb)(void) = user_cb;
            if (cb) {
                cb();
            }
            due = 1;
        } else if (!ch32rv_rtc_alarm_in_sleep) {
            /* Early (a sleep's alarm, or settimeofday() moved the clock): the
             * register back to the sketch's. A sleep re-arms it itself. */
            ch32rv_rtc_alarm(ch32rv_rtc_counter_for(when));
        }
    }
    /* attachExtiLine()'s callback: every edge but a sleep's own alarm. */
    if (hook && pending && (due || !ch32rv_rtc_alarm_in_sleep)) {
        hook(pending);
    }
}

/* The alarm's EXTI line (rising edge) and interrupt on. */
static void lines_on(void)
{
    CH32RV_EXTI_RTENR |= 1u << CH32RV_RTC_ALARM_LINE;
    CH32RV_EXTI_INTENR |= 1u << CH32RV_RTC_ALARM_LINE;
    ch32rv_irq_enable(CH32RV_IRQN_RTCAlarm);
}

void ch32rv_rtc_alarm_restore(void)
{
    const uint32_t when = user_when;
    if (!when) {
        return;
    }
    lines_on();
    if (wall_now() >= when) {
        /* It came while the register held something else: the interrupt
         * runs it now, in the same context as always. */
        PFIC_IPSR(CH32RV_IRQN_RTCAlarm >> 5) = 1u << (CH32RV_IRQN_RTCAlarm & 31u);
    } else {
        ch32rv_rtc_alarm(ch32rv_rtc_counter_for(when));
    }
}

void ch32rv_rtc_alarm_rearm(void)
{
    ch32rv_rtc_alarm_restore();
}

void ch32rv_rtc_alarm_arm(uint32_t sec)
{
    const uint32_t when = user_when;
    if (when) {
        const uint32_t c = ch32rv_rtc_counter_for(when);
        if (c < sec) {
            sec = c;
        }
    }
    ch32rv_rtc_alarm(sec);
}

int ch32rv_rtc_user_alarm(uint32_t when, void (*cb)(void))
{
    if (!ch32rv_rtc_begin()) {
        return 0;
    }
    ch32rv_irq_disable(CH32RV_IRQN_RTCAlarm);
    user_when = 0;
    user_cb = cb;
    if (!when) {
        RTC_CTLRH &= ~RTC_CTLRH_ALRIE;
        PFIC_IPRR(CH32RV_IRQN_RTCAlarm >> 5) = 1u << (CH32RV_IRQN_RTCAlarm & 31u);
        if (ch32rv_rtc_alarm_line_hook) {
            ch32rv_irq_enable(CH32RV_IRQN_RTCAlarm);   /* attachExtiLine() holds the line */
        } else {
            CH32RV_EXTI_INTENR &= ~(1u << CH32RV_RTC_ALARM_LINE);
        }
        return 1;
    }
    user_when = when;
    ch32rv_rtc_alarm_restore();
    return 1;
}

uint32_t ch32rv_rtc_user_alarm_at(void)
{
    return user_when;
}

#endif
