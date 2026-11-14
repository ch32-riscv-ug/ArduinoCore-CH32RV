/* SysTick as a free-running tick count, for waits finer than micros()'s
 * microsecond (SoftWire's half clocks). It counts up and wraps at 2^32;
 * compare differences as int32_t. CH32V103's SysTick counts HCLK / 8, every
 * other family's HCLK. */
#pragma once

#include <stdint.h>

#include "internal/ch32rv_clock.h"
#include "internal/ch32rv_registers.h"

#if CH32RV_SYSTICK_V103
#define CH32RV_SYSTICK_HZ (F_CPU / 8u)
#else
#define CH32RV_SYSTICK_HZ (CH32RV_HCLK_HZ)   /* SysTick counts HCLK (STCLK) */
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t ch32rv_ticks(void)
{
    return CH32RV_SYSTICK_CNT;
}

#ifdef __cplusplus
}
#endif
