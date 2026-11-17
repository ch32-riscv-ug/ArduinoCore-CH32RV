/* The programmable voltage detector (ch32rv_pvd.c): a callback when VDD falls
 * below a threshold - not a brownout reset (ESP32's brownout detector resets
 * the chip; this only tells the sketch, which may save its state). */
#pragma once

#include <stdint.h>

#include "pins_arduino.h"
#include "internal/ch32rv_exti_line.h"

/* The levels come from the variant (CH32RV_PVD_*, generated), the line from
 * ch32rv_exti_line.h: CH32H41x has levels (device-data pvd_levels.csv) but no
 * PVD line in its reference manual or EVT, CH32M030 and CH32V004 no levels
 * (their datasheets have no PVD table). */
#if defined(CH32RV_PVD_LEVELS) && defined(CH32RV_EXTI_PVD)
#define CH32RV_HAS_PVD 1
#else
#define CH32RV_HAS_PVD 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* PVD on at the lowest threshold at or above mv (the highest when mv is above
 * them all), cb run in the PVD interrupt each time VDD falls below it - once
 * at once when it already is. Returns the threshold in mV. */
uint32_t ch32rv_pvd_set(uint32_t mv, void (*cb)(void));
void ch32rv_pvd_clear(void);
/* VDD is below the threshold now (PVDO). */
int ch32rv_pvd_low(void);
/* The threshold of PLS value `level`, 0 past the last. */
uint32_t ch32rv_pvd_level_mv(uint8_t level);

#ifdef __cplusplus
}
#endif
