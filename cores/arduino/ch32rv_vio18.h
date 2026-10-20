/* CH32H41x VIO18: the supply of the high-speed pins (PA13-15, PB0-1, PB10-14,
 * PC0-3, PC6-12, PD0-7, PD9-15, PE0-1, PE7-15, PF0-5, PF11-14), from the
 * chip's own LDO (PWR_CTLR VSEL_VIO18). It powers up at the voltage the XO
 * strap selects - 1.2 V on the evaluation board, measured 1.36 V - so those
 * pins drive that as their high until it is raised. A board can have 1.8 V
 * parts on VIO18, so nothing here changes it unless asked: the VIO18 Supply
 * menu (CH32RV_VIO18_MV) or CH32RV.setVio18Millivolts(). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
#define CH32RV_HAS_VIO18 1

#ifdef __cplusplus
extern "C" {
#endif
/* 1200, 1800, 2500 or 3300 (mV); false for anything else. */
bool ch32rv_vio18_set_mv(uint32_t mv);
/* The voltage software set, or 0 while the XO strap's still applies. */
uint32_t ch32rv_vio18_mv(void);
#ifdef __cplusplus
}
#endif
#endif
