/* The RTC alarm's work in its interrupt, the sketch's alarm (CH32RV.setAlarm()) and the
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

static uint32_t wall_now(void)
{
    uint32_t s, m;
    ch32rv_rtc_get(&s, &m);
    return s;
}

/* The alarm's part of its interrupt (the vector is ch32rv_rtc_alarm_irq.c's,
 * which has cleared the line). 0 when the edge was a sleep's own alarm, which
 * attachExtiLine(CH32RV_EXTI_RTC_ALARM)'s callback is not shown. */
int ch32rv_rtc_alarm_isr(void)
{
    RTC_CTLRL &= ~RTC_CTLRL_ALRF;
    ch32rv_rtc_alarm_events++;
    const uint32_t when = user_when;
    if (when && wall_now() >= when) {
        user_when = 0;                         /* once: setAlarm() again for the next */
        void (*const cb)(void) = user_cb;
        if (cb) {
            cb();
        }
        return 1;
    }
    if (when && !ch32rv_rtc_alarm_in_sleep) {
        /* Early (a sleep's alarm, or settimeofday() moved the clock): the
         * register back to the sketch's. A sleep re-arms it itself. */
        ch32rv_rtc_alarm(ch32rv_rtc_counter_for(when));
    }
    return !ch32rv_rtc_alarm_in_sleep;
}

void ch32rv_rtc_alarm_restore(void)
{
    const uint32_t when = user_when;
    if (!when) {
        return;
    }
    ch32rv_rtc_alarm_lines_on();               /* ch32rv_rtc_alarm_irq.c */
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
        ch32rv_rtc_alarm_lines_off();          /* on again if attachExtiLine() holds it */
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
