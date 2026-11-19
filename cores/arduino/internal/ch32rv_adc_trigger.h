/* What starts an ADC1 regular-group round besides software: a timer (its
 * TRGO or a capture / compare event) or a pad's edge, as each family's
 * CTLR2.EXTSEL names them (ch32rv_adc_trigger.c sets the source up;
 * ch32rv_adc_run.h runs the ADC on it).
 *
 * The sources differ between families. The timers are the variant's
 * CH32RV_ADC_TIMER_SOURCES, generated from ch32-device-data's
 * index/adc_triggers.csv; the pads below are written here (the CSV also lists
 * value 6's other sources - V205's TKEY / OPA, V006's OPA - which this core
 * does not use). For reference, from the EVT headers' ADC_ExternalTrigConv_*:
 *
 *   F1 layout (V103, V20x, V205, V30x, V4x7, L103 / M103, H41x, X305 / X315):
 *     0 TIM1 CC1, 1 TIM1 CC2, 2 TIM1 CC3, 3 TIM2 CC2, 4 TIM3 TRGO, 5 TIM4 CC4,
 *     6 EXTI line 11 (X305 / X315: line 12), 7 software.
 *     6 is TIM8's TRGO instead when AFIO_PCFR1.ADC1_ETRGREG_REMAP is set
 *     (V20x / V30x / V4x7 / H41x); it is left at its reset 0.
 *   V00x (V002 .. V007, M007): 0 TIM1 TRGO, 1 TIM1 CC1, 2 TIM1 CC2,
 *     3 TIM2 TRGO, 4 TIM2 CC1, 5 TIM2 CC2, 6 a pad (PD3, or PC2 with
 *     AFIO_PCFR1 bit 18 on V003 / bit 19 elsewhere - routes.csv
 *     afio-adc*-etrgreg-remap), 7 software.
 *   X033 / X035: as V00x but 5 TIM3 CC1 and 6 EXTI line 11.
 *   M030 (EXTSEL is CTLR2[19:16], four bits): 1 TIM1 CC4, 2 TIM1 CC5,
 *     3..6 TIM2 CC1..CC4, 7 TIM3 CC1, 8 TIM3 CC2, 9 a pad (PA14, or PB6 with
 *     AFIO_PCFR1 bit 15), 10 TIM1 CC4 or CC5.
 *
 * A pad source passes through the EXTI line of its bit (the EVTs'
 * ExtLines_Trigger set the line up in event mode with its edge), which is
 * how the edge is chosen here too. The ADC starts on the rising edge of what
 * reaches it (every RM: "only its rising edge can start the conversion");
 * through EXTI line 11 that is one start per edge the line is set to, both
 * on CHANGE (V203, V103: 32 conversions for 16 pulses), but V00x's PD3 / PC2
 * gave one per pulse on CHANGE where RISING and FALLING each gave theirs
 * (V003, V006, 2026-11-17): CHANGE is refused there
 * (CH32RV_ADC_TRIGGER_ONE_EDGE). M030's PA14 / PB6 is not measured yet. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "internal/ch32rv_pins.h"
#include "internal/ch32rv_registers.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CH32RV_SERIES_CH32M030)
#define CH32RV_ADC_EXTSEL_SHIFT 16u
#define CH32RV_ADC_EXTSEL_MASK  (0xFu << 16)
#else
#define CH32RV_ADC_EXTSEL_SHIFT 17u
#define CH32RV_ADC_EXTSEL_MASK  (0x7u << 17)
#endif
#define CH32RV_ADC_EXTSEL(v)    ((uint32_t)(v) << CH32RV_ADC_EXTSEL_SHIFT)

/* A timer source: the timer's number, its event (0 TRGO from the update,
 * 1..4 a channel's compare) and the EXTSEL value. Listed in the order they
 * are tried: TRGO first (no channel taken), the high-numbered timers before
 * TIM1 (whose pads carry the default PWM). */
#define CH32RV_ADC_TRGO 0u
/* CH32RV_ADC_TIMER_SOURCES(X) comes from the variant (generated from
 * ch32-device-data's adc_triggers.csv, 2026-11-19; it had been written here
 * from the same EVT tables). */
#if !defined(CH32RV_ADC_TIMER_SOURCES)
#define CH32RV_ADC_TIMER_SOURCES(X)
#endif

/* The pad source: its EXTSEL value; which pads (CH32RV_ADC_TRIGGER_LINE: any
 * port's pad of that bit; or CH32RV_ADC_TRIGGER_PADS(X): X(pad, remap value)
 * with the AFIO_PCFR1 bit CH32RV_ADC_TRIGGER_REMAP_BIT choosing). */
#if defined(CH32RV_SERIES_CH32M030)
#define CH32RV_ADC_TRIGGER_EXTSEL 9u
#define CH32RV_ADC_TRIGGER_REMAP_BIT 15u
#define CH32RV_ADC_TRIGGER_PADS(X) X(CH32RV_PIN(0, 14), 0) X(CH32RV_PIN(1, 6), 1)
#elif defined(CH32RV_SERIES_CH32V003)
#define CH32RV_ADC_TRIGGER_EXTSEL 6u
#define CH32RV_ADC_TRIGGER_REMAP_BIT 18u
#define CH32RV_ADC_TRIGGER_ONE_EDGE 1
#define CH32RV_ADC_TRIGGER_PADS(X) X(CH32RV_PIN(3, 3), 0) X(CH32RV_PIN(2, 2), 1)
#elif defined(CH32RV_SERIES_CH32V002) || defined(CH32RV_SERIES_CH32V004) || defined(CH32RV_SERIES_CH32V005) || \
    defined(CH32RV_SERIES_CH32V006) || defined(CH32RV_SERIES_CH32V007) || defined(CH32RV_SERIES_CH32M007)
#define CH32RV_ADC_TRIGGER_EXTSEL 6u
#define CH32RV_ADC_TRIGGER_REMAP_BIT 19u
#define CH32RV_ADC_TRIGGER_ONE_EDGE 1
#define CH32RV_ADC_TRIGGER_PADS(X) X(CH32RV_PIN(3, 3), 0) X(CH32RV_PIN(2, 2), 1)
#elif defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315)
#define CH32RV_ADC_TRIGGER_EXTSEL 6u
#define CH32RV_ADC_TRIGGER_LINE 12u
#else
#define CH32RV_ADC_TRIGGER_EXTSEL 6u
#define CH32RV_ADC_TRIGGER_LINE 11u
#endif

/* A timer set up to trigger the ADC (ch32rv_adc_trigger.c). */
typedef struct {
    uint8_t timer;        /* 0 when none */
    uint8_t event;        /* CH32RV_ADC_TRGO or the channel */
    uint8_t extsel;
    uint32_t prescaler;   /* the timer's clock is divided by prescaler x period */
    uint32_t period;
} ch32rv_adc_timer_trigger_t;

/* A free timer among the family's sources, set to trigger hz / per rounds a
 * second (the pair keeps a rate below 1 Hz, or a fraction, exact): its
 * counter set, not started. False
 * when none is free, or the rate is out of every timer's reach (faster than
 * its clock, or slower than its 16 bits of prescaler and of counter). */
bool ch32rv_adc_timer_trigger_begin(ch32rv_adc_timer_trigger_t *t, uint32_t hz, uint32_t per);
/* The rate it made, in rounds per second x 1000 (the timer's clock over its
 * divisions, rounded). */
uint32_t ch32rv_adc_timer_trigger_millihz(const ch32rv_adc_timer_trigger_t *t);
void ch32rv_adc_timer_trigger_start(const ch32rv_adc_timer_trigger_t *t);
void ch32rv_adc_timer_trigger_stop(const ch32rv_adc_timer_trigger_t *t);
/* Stops and frees the timer. */
void ch32rv_adc_timer_trigger_end(ch32rv_adc_timer_trigger_t *t);

/* Whether `pin` (a sketch's pin number) can start the ADC by its edge. */
bool ch32rv_adc_pin_trigger_valid(uint32_t pin);
/* Whether the trigger pad starts a round on `mode` (RISING, FALLING or
 * CHANGE): not CHANGE where the pad gives one edge only. */
static inline bool ch32rv_adc_pin_trigger_mode_ok(uint8_t mode)
{
#ifdef CH32RV_ADC_TRIGGER_ONE_EDGE
    return mode == RISING || mode == FALLING;
#else
    return mode == RISING || mode == FALLING || mode == CHANGE;
#endif
}
/* The pad's EXTI line set to give the ADC its edge (`mode` RISING, FALLING
 * or CHANGE, as attachInterrupt()), the port and any remap selected; the
 * EXTSEL value in *extsel. False when the pad is no trigger source, or the
 * mode one it cannot give (ch32rv_adc_pin_trigger_mode_ok()). The pad's
 * mode is the sketch's (pinMode(); after reset it is a floating input). The
 * line's interrupt is not touched, so attachInterrupt() on the same pad
 * still works and shares its edge setting. */
bool ch32rv_adc_pin_trigger_begin(uint32_t pin, uint8_t mode, uint8_t *extsel);
void ch32rv_adc_pin_trigger_end(uint32_t pin);

#ifdef __cplusplus
}
#endif
