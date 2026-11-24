/* Helpers shared by CH32RVTimerOutput's classes (not a public header). */
#pragma once

#include <Arduino.h>

#include "CH32RVTimer.h"
#include "internal/ch32rv_registers.h"

namespace ch32rv_timer_output {


#define TIM_CTLR2(b) CH32RV_REG16((b) + 0x04u)   /* output idle states (OISx) */

constexpr uint16_t CTLR1_OPM = 1u << 3;        /* one-pulse: CEN clears at the update */
constexpr uint16_t SWEVGR_UG = 1u << 0;
constexpr uint16_t INT_BIF = 1u << 7;          /* INTFR: the break came */
constexpr uint16_t BDTR_OSSI = 1u << 10;       /* outputs driven to idle while MOE is off */
constexpr uint16_t BDTR_OSSR = 1u << 11;
constexpr uint16_t BDTR_BKE = 1u << 12;
constexpr uint16_t BDTR_BKP = 1u << 13;        /* break active high */
constexpr uint8_t OCM_FORCE_INACTIVE = 4, OCM_PWM1 = 6, OCM_PWM2 = 7;
constexpr uint8_t OCPE = 0x08;                 /* compare preload */
constexpr uint16_t SMS_TRIGGER = 6;            /* SMCFGR: the trigger starts the counter */
constexpr uint16_t TS_TI1FP1 = 5u << 4, TS_TI2FP2 = 6u << 4;

/* Channel ch's half of its CHCTLR (1-2 share one, 3-4 the other; the even
 * channel in the high byte). */
inline void channelMode(uint32_t base, uint8_t ch, uint8_t value)
{
    volatile uint16_t &reg = ch <= 2 ? CH32RV_TIM_CHCTLR1(base) : CH32RV_TIM_CHCTLR2(base);
    const unsigned shift = ((ch - 1u) & 1u) * 8u;
    reg = (uint16_t)((reg & ~(0xFFu << shift)) | ((uint16_t)value << shift));
}

/* Channel ch's four bits of CCER: CCxE, CCxP, CCxNE, CCxNP. */
inline void channelEnable(uint32_t base, uint8_t ch, uint8_t bits)
{
    const unsigned shift = (ch - 1u) * 4u;
    CH32RV_TIM_CCER(base) = (uint16_t)((CH32RV_TIM_CCER(base) & ~(0xFu << shift)) | ((uint16_t)bits << shift));
}

inline void setCompare(uint32_t base, uint8_t ch, uint32_t value, bool wide)
{
    if (wide) {
        CH32RV_TIM_CHCVR32(base, ch) = value;
    } else {
        CH32RV_TIM_CHCVR(base, ch) = (uint16_t)(value > 0xFFFFu ? 0xFFFFu : value);
    }
}

/* A period of `cycles` timer clocks as the smallest prescaler whose steps
 * fit the counter, and those steps. False when even the largest does not. */
inline bool split(uint64_t cycles, bool wide, uint32_t &prescale, uint32_t &steps)
{
    const uint64_t span = wide ? 0x100000000ull : 0x10000ull;
    uint64_t p = (cycles + span - 1u) / span;
    if (p == 0u) {
        p = 1u;
    }
    if (p > 65536u || cycles < 2u) {
        return false;
    }
    prescale = (uint32_t)p;
    steps = (uint32_t)((cycles + p / 2u) / p);
    if (steps > span - 1u) {
        steps = (uint32_t)(span - 1u);
    }
    return true;
}

/* The timer clocks in `us` microseconds. */
inline uint64_t clocksIn(uint8_t timer, uint32_t us)
{
    return (uint64_t)us * ch32rv_timer_clock_hz(timer) / 1000000u;
}

/* A pad whose timer output stopped: a plain low output. */
inline void padLow(uint8_t pin)
{
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
}

/* BDTR's dead-time field for at least `nanoseconds` at the timer clock, and
 * the clocks it gives. */
inline uint8_t deadTimeField(uint32_t clock, uint32_t nanoseconds, uint32_t &ticks)
{
    uint8_t field;
    /* Clocks of the dead-time generator (CKD 0: the timer clock), rounded up. */
    const uint32_t want = (uint32_t)(((uint64_t)nanoseconds * clock + 999999999u) / 1000000000u);
    if (want <= 127u) {
        field = (uint8_t)want;
        ticks = want;
    } else if (want <= 254u) {
        const uint32_t n = (want + 1u) / 2u;               /* (64 + DTG[5:0]) x 2 */
        field = (uint8_t)(0x80u | (n - 64u));
        ticks = n * 2u;
    } else if (want <= 504u) {
        const uint32_t n = (want + 7u) / 8u;               /* (32 + DTG[4:0]) x 8 */
        field = (uint8_t)(0xC0u | (n - 32u));
        ticks = n * 8u;
    } else {
        uint32_t n = (want + 15u) / 16u;                   /* (32 + DTG[4:0]) x 16 */
        if (n > 63u) {
            n = 63u;
        }
        field = (uint8_t)(0xE0u | (n - 32u));
        ticks = n * 16u;
    }
    return field;
}

}  // namespace ch32rv_timer_output
