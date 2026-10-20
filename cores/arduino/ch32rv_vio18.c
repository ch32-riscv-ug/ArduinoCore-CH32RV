/* CH32H41x VIO18 (ch32rv_vio18.h). PWR_CTLR: VIO_SW_CR (bit 9) hands the LDO
 * to software, VSEL_VIO18 [12:10] = 0 1.2 V, 1 1.8 V, 2 2.5 V, 3 3.3 V
 * (CH32H417RM 3.4.1). Measured on the evaluation board (2026-10-21): PC0 /
 * PB0 highs of 1.36, 1.83, 2.54 and 3.31 V. */
#include "ch32rv_vio18.h"

#ifdef CH32RV_HAS_VIO18

#include "pins_arduino.h"        /* CH32RV_CLKEN_PWR */
#include "ch32rv_registers.h"

#define PWR_CTLR       CH32RV_REG32(0x40007000u)
#define VIO_SW_CR      (1u << 9)
#define VSEL_SHIFT     10u
#define VSEL_MASK      (7u << VSEL_SHIFT)

static const uint16_t vio18_mv[4] = {1200, 1800, 2500, 3300};

bool ch32rv_vio18_set_mv(uint32_t mv)
{
    for (uint32_t sel = 0; sel < 4u; sel++) {
        if (vio18_mv[sel] == mv) {
            ch32rv_clock_enable(PWR);
            PWR_CTLR = (PWR_CTLR & ~VSEL_MASK) | (sel << VSEL_SHIFT) | VIO_SW_CR;
            return true;
        }
    }
    return false;
}

uint32_t ch32rv_vio18_mv(void)
{
    ch32rv_clock_enable(PWR);
    const uint32_t c = PWR_CTLR;
    if (!(c & VIO_SW_CR)) {
        return 0;
    }
    const uint32_t sel = (c & VSEL_MASK) >> VSEL_SHIFT;
    return sel < 4u ? vio18_mv[sel] : 0;   /* 4..7: powered down */
}

#endif
