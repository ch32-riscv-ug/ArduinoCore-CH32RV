/* The programmable voltage detector: CH32RV.setLowVoltageDetect(). The
 * thresholds are the datasheet's typical falling-edge values per PLS
 * (CH32RV_PVD_FALL_MV, generated into the variant); the PVD's output rises
 * when VDD falls below the threshold, and its EXTI line's rising edge runs
 * the callback (ch32rv_exti_line.c). Not on CH32H41x (no PVD interrupt),
 * CH32M030 (no PVD) and the series the data gives no levels for. */
#include "Arduino.h"
#include "internal/ch32rv_exti_line.h"
#include "internal/ch32rv_pvd.h"

#if CH32RV_HAS_PVD

#define PWR_CTLR  CH32RV_REG32(0x40007000u)
#define PWR_CSR   CH32RV_REG32(0x40007004u)
#define PWR_PVDE  (1u << 4)
#define PVDO      (1u << CH32RV_PVD_PVDO_BIT)
#define PLS_SHIFT 5u
#define PVD_BIT   (1u << CH32RV_EXTI_PVD)

static const uint16_t fall_mv[CH32RV_PVD_LEVELS] = CH32RV_PVD_FALL_MV;

uint32_t ch32rv_pvd_level_mv(uint8_t level)
{
    return level < CH32RV_PVD_LEVELS ? fall_mv[level] : 0u;
}

uint32_t ch32rv_pvd_set(uint32_t mv, void (*cb)(void))
{
    /* The lowest threshold at or above mv (the warning no later than asked),
     * the highest when mv is above them all. */
    uint8_t pls = CH32RV_PVD_LEVELS - 1u;
    for (uint8_t i = 0; i < CH32RV_PVD_LEVELS; i++) {
        if (fall_mv[i] >= mv) {
            pls = i;
            break;
        }
    }
    ch32rv_clock_enable(PWR);
    ch32rv_irq_disable(CH32RV_IRQN_PVD);
    PWR_CTLR = (PWR_CTLR & ~CH32RV_PVD_PLS_MASK) | ((uint32_t)pls << PLS_SHIFT)
#if CH32RV_PVD_PVDE
               | PWR_PVDE
#endif
        ;
    ch32rv_exti_line_attach(CH32RV_EXTI_PVD, cb, RISING);
    ch32rv_irq_disable(CH32RV_IRQN_PVD);
    /* Already below it: no edge will come, so one is made - the callback runs
     * once now, as setAlarm()'s for a time already past. An edge that came
     * meanwhile is pending and is the one. */
    if ((PWR_CSR & PVDO) && !(CH32RV_EXTI_INTFR & PVD_BIT)) {
        CH32RV_EXTI_SWIEVR = PVD_BIT;
    }
    ch32rv_irq_enable(CH32RV_IRQN_PVD);
    return fall_mv[pls];
}

void ch32rv_pvd_clear(void)
{
    ch32rv_exti_line_detach(CH32RV_EXTI_PVD);
#if CH32RV_PVD_PVDE
    PWR_CTLR &= ~PWR_PVDE;
#endif
}

int ch32rv_pvd_low(void)
{
    ch32rv_clock_enable(PWR);
    return (PWR_CSR & PVDO) != 0u;
}

#endif
