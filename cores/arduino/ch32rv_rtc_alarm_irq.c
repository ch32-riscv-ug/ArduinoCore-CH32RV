/* The RTC alarm's vector, shared by the sketch's alarm and the sleeps
 * (ch32rv_rtc_alarm.c) and attachExtiLine(CH32RV_EXTI_RTC_ALARM)
 * (ch32rv_exti_line.c): each brings this file, and either may be linked
 * without the other. */
#include "Arduino.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_rtc.h"
#include "internal/ch32rv_exti_line.h"
#include "internal/ch32rv_interrupt.h"

#ifdef CH32RV_HAS_RTC

#define ALARM_BIT (1u << CH32RV_RTC_ALARM_LINE)

/* attachExtiLine() holds the line: it stays on when the others let it go. */
volatile uint8_t ch32rv_rtc_alarm_generic;

/* ch32rv_rtc_alarm.c: the alarm's own work; 0 when the edge was a sleep's
 * own (not shown to attachExtiLine()'s callback). */
int ch32rv_rtc_alarm_isr(void) __attribute__((weak));
uint32_t ch32rv_rtc_user_alarm_at(void) __attribute__((weak));
/* ch32rv_exti_line.c. */
void ch32rv_exti_line_deliver(uint32_t pending) __attribute__((weak));

CH32RV_INTERRUPT void RTCAlarm_IRQHandler(void)
{
    const uint32_t pending = CH32RV_EXTI_INTFR & ALARM_BIT;
    CH32RV_EXTI_INTFR = ALARM_BIT;
    int show = 1;
    if (ch32rv_rtc_alarm_isr) {
        show = ch32rv_rtc_alarm_isr();
    }
    if (pending && show && ch32rv_exti_line_deliver) {
        ch32rv_exti_line_deliver(pending);
    }
}

void ch32rv_rtc_alarm_lines_on(void)
{
    CH32RV_EXTI_RTENR |= ALARM_BIT;            /* the alarm flag's rising edge */
    CH32RV_EXTI_INTENR |= ALARM_BIT;
    ch32rv_irq_enable(CH32RV_IRQN_RTCAlarm);
}

void ch32rv_rtc_alarm_lines_off(void)
{
    if (ch32rv_rtc_alarm_generic || (ch32rv_rtc_user_alarm_at && ch32rv_rtc_user_alarm_at())) {
        ch32rv_rtc_alarm_lines_on();
        return;
    }
    ch32rv_irq_disable(CH32RV_IRQN_RTCAlarm);
    CH32RV_EXTI_INTENR &= ~ALARM_BIT;
}

#endif
