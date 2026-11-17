/* The EXTI lines that are not pins (internal/ch32rv_exti_line.h): a callback
 * per line, run in the line's interrupt, and the sleeps' wake on it
 * (ch32rv_exti_seen). Linked only by a sketch that attaches one: the vectors
 * defined here then replace the default handlers, the RTC alarm's excepted
 * (ch32rv_rtc_alarm_irq.c owns it and hands its edges here). */
#include "Arduino.h"
#include "internal/ch32rv_exti_line.h"
#include "internal/ch32rv_bits.h"
#include "internal/ch32rv_interrupt.h"

#ifdef CH32RV_EXTI_LINE_MASK

extern volatile uint32_t ch32rv_exti_seen;     /* ch32rv_exti_seen.c */

static void (*volatile line_cb[CH32RV_EXTI_LINE_LAST - CH32RV_EXTI_LINE_FIRST + 1])(void);

static int served(uint8_t line)
{
    return line < 32u && ((CH32RV_EXTI_LINE_MASK >> line) & 1u);
}

/* The flags are cleared before the callbacks run, so an edge that comes
 * while one runs is not lost. */
void ch32rv_exti_line_deliver(uint32_t pending)
{
    ch32rv_exti_seen |= pending;
    while (pending) {
        const unsigned line = ch32rv_ctz32(pending);
        pending &= ~(1u << line);
        void (*const cb)(void) = line_cb[line - CH32RV_EXTI_LINE_FIRST];
        if (cb) {
            cb();
        }
    }
}

static void line_isr(uint32_t lines)
{
    const uint32_t pending = CH32RV_EXTI_INTFR & lines;
    CH32RV_EXTI_INTFR = pending;
    ch32rv_exti_line_deliver(pending);
}

#define CH32RV_EXTI_LINE_ISR(name, lines)          \
    CH32RV_INTERRUPT void name##_IRQHandler(void) \
    {                                             \
        line_isr(lines);                          \
    }
CH32RV_EXTI_LINE_VECTORS(CH32RV_EXTI_LINE_ISR)
#undef CH32RV_EXTI_LINE_ISR

int ch32rv_exti_line_attach(uint8_t line, void (*cb)(void), int mode)
{
    if (!served(line) || (mode != RISING && mode != FALLING && mode != CHANGE)) {
        return 0;
    }
    const uint32_t bit = 1u << line;
    line_cb[line - CH32RV_EXTI_LINE_FIRST] = cb;
    if (mode != FALLING) {
        CH32RV_EXTI_RTENR |= bit;
    } else {
        CH32RV_EXTI_RTENR &= ~bit;
    }
    if (mode != RISING) {
        CH32RV_EXTI_FTENR |= bit;
    } else {
        CH32RV_EXTI_FTENR &= ~bit;
    }
#ifdef CH32RV_EXTI_RTC_ALARM
    if (line == CH32RV_EXTI_RTC_ALARM) {
        /* Shared with setAlarm() and the sleeps: the flag is theirs too. */
        ch32rv_rtc_alarm_generic = 1;
        ch32rv_rtc_alarm_lines_on();
        return 1;
    }
#endif
    CH32RV_EXTI_INTFR = bit;                   /* nothing left from before */
    CH32RV_EXTI_INTENR |= bit;
#define CH32RV_EXTI_LINE_ENABLE(name, lines) \
    if ((lines) & bit) ch32rv_irq_enable(CH32RV_IRQN_##name);
    CH32RV_EXTI_LINE_VECTORS(CH32RV_EXTI_LINE_ENABLE)
#undef CH32RV_EXTI_LINE_ENABLE
    return 1;
}

void ch32rv_exti_line_detach(uint8_t line)
{
    if (!served(line)) {
        return;
    }
    const uint32_t bit = 1u << line;
    line_cb[line - CH32RV_EXTI_LINE_FIRST] = 0;
#ifdef CH32RV_EXTI_RTC_ALARM
    if (line == CH32RV_EXTI_RTC_ALARM) {
        /* Its edge (rising) and the line stay for setAlarm() / the sleeps,
         * which turn them on again themselves; off unless they hold it. */
        ch32rv_rtc_alarm_generic = 0;
        ch32rv_rtc_alarm_lines_off();
        return;
    }
#endif
    /* The vector is left on: on V407 / V467 two lines share two. */
    CH32RV_EXTI_INTENR &= ~bit;
    CH32RV_EXTI_RTENR &= ~bit;
    CH32RV_EXTI_FTENR &= ~bit;
    CH32RV_EXTI_INTFR = bit;
}

void (*ch32rv_exti_line_callback(uint8_t line))(void)
{
    return served(line) ? line_cb[line - CH32RV_EXTI_LINE_FIRST] : 0;
}

void ch32rv_exti_line_trigger(uint8_t line)
{
    if (served(line)) {
        CH32RV_EXTI_SWIEVR = 1u << line;
    }
}

#endif
