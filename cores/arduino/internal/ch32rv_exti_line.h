/* The EXTI lines that are not pins: the PVD, the RTC alarm, the USB / ETH /
 * LPTIM / comparator wake-ups - CH32RV.attachExtiLine() (libraries/CH32RV)
 * on ch32rv_exti_line.c. A pin's line is attachInterrupt()'s
 * (wiring_interrupts.c).
 *
 * Each line is named by what drives it, CH32RV_EXTI_<NAME> = its number, and
 * only where the series has it and a vector reaches it: a sketch tests
 * `#ifdef CH32RV_EXTI_PVD`, and a line the series lacks does not compile.
 *
 * The table is written here by hand: ch32-device-data has no EXTI line table
 * (reported). Its sources are the reference manuals' "EXTI interrupt mapping"
 * tables (CH32xRM 9-3, CH32FV2x_V3xRM, CH32V205RM, CH32V407RM, CH32L103RM,
 * CH32H417RM, CH32X315RM, CH32X035RM, CH32M030RM, CH32V003RM, CH32V00XRM) and
 * the EVT's ch32*_exti.h comments; where they disagree it says so below. The
 * vectors are the generated irqn_<variant>.h names, so a die variant without
 * the vector (CH32V303's USB wake-up, CH32V203 D6's ETH) leaves the line out.
 *
 * Left out on purpose: the AWU's line (V00x 9, X035 27, M030 17) - the timer
 * of lightSleep() owns it and its handler (ch32rv_power.c); X035's lines 24 /
 * 25 (PC18 / PC19, pins). */
#pragma once

#include <stdint.h>

#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_rtc.h"

/* ---- the lines, per series -------------------------------------------- */
#if defined(CH32RV_SERIES_CH32V003) || defined(CH32RV_SERIES_CH32V002) || defined(CH32RV_SERIES_CH32V004) || \
    defined(CH32RV_SERIES_CH32V005) || defined(CH32RV_SERIES_CH32V006) || defined(CH32RV_SERIES_CH32V007) || \
    defined(CH32RV_SERIES_CH32M007)
#define CH32RV_EXTI_PVD 8u
#define CH32RV_EXTI_LINE_VECTORS(X) \
    X(PVD, 1u << 8)

#elif defined(CH32RV_SERIES_CH32V103)
/* Line 18 is CH32F103's USBD; line 19 the USBFS (USBHD) wake-up, whose only
 * vector is USBWakeUp. */
#define CH32RV_EXTI_PVD 16u
#define CH32RV_EXTI_USBFS_WAKEUP 19u
#define CH32RV_EXTI_LINE_VECTORS(X) \
    X(PVD, 1u << 16) \
    X(USBWakeUp, 1u << 19)

#elif defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208)
/* 18 the USBD's wake-up from suspend, 19 the ETH's, 20 the USBFS's, 21 the
 * internal 32K oscillator's calibration (D8 / D8W only, as ETH). */
#define CH32RV_EXTI_PVD 16u
#define CH32RV_EXTI_USBD_WAKEUP 18u
#define CH32RV_EXTI_USBFS_WAKEUP 20u
#ifdef CH32RV_IRQN_ETHWakeUp
#define CH32RV_EXTI_ETH_WAKEUP 19u
#endif
#ifdef CH32RV_IRQN_OSCWakeUp
#define CH32RV_EXTI_OSC32K_WAKEUP 21u
#endif
#define CH32RV_EXTI_LINE_VECTORS(X) \
    X(PVD, 1u << 16) \
    X(USBWakeUp, 1u << 18) \
    X(USBFSWakeUp, 1u << 20) \
    CH32RV_EXTI_V20X_D8(X)
#ifdef CH32RV_IRQN_ETHWakeUp
#define CH32RV_EXTI_V20X_D8(X) X(ETHWakeUp, 1u << 19) X(OSCWakeUp, 1u << 21)
#else
#define CH32RV_EXTI_V20X_D8(X)
#endif

#elif defined(CH32RV_SERIES_CH32V303) || defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || \
      defined(CH32RV_SERIES_CH32V317)
/* 18 the USBFS (OTG) wake-up; 19 the ETH's and 20 the USBHS's on the D8C die
 * only (V305 / V307 / V317): V303 (D8) has neither vector. */
#define CH32RV_EXTI_PVD 16u
#define CH32RV_EXTI_USBFS_WAKEUP 18u
#ifdef CH32RV_IRQN_USBHSWakeup
#define CH32RV_EXTI_ETH_WAKEUP 19u
#define CH32RV_EXTI_USBHS_WAKEUP 20u
#define CH32RV_EXTI_LINE_VECTORS(X) \
    X(PVD, 1u << 16) \
    X(USBWakeUp, 1u << 18) \
    X(ETH_WKUP, 1u << 19) \
    X(USBHSWakeup, 1u << 20)
#else
#define CH32RV_EXTI_LINE_VECTORS(X) \
    X(PVD, 1u << 16) \
    X(USBWakeUp, 1u << 18)
#endif

#elif defined(CH32RV_SERIES_CH32V205)
/* Line 21: the EVT's header says the USBHS wake-up, the reference manual
 * LPTIM's - V205 has no LPTIM, and its only vector is USBHSWakeup. */
#define CH32RV_EXTI_PVD 16u
#define CH32RV_EXTI_USBPD_WAKEUP 19u
#define CH32RV_EXTI_USBFS_WAKEUP 20u
#define CH32RV_EXTI_USBHS_WAKEUP 21u
#define CH32RV_EXTI_COMP_WAKEUP 22u
#define CH32RV_EXTI_LVDS_WAKEUP 23u
#define CH32RV_EXTI_LINE_VECTORS(X) \
    X(PVD, 1u << 16) \
    X(USBPDWakeUp, 1u << 19) \
    X(USBFSWakeUp, 1u << 20) \
    X(USBHSWakeup, 1u << 21) \
    X(CMPWakeUp, 1u << 22) \
    X(LVDSWakeup, 1u << 23)

#elif defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
/* The two USBHS wake-ups: the vectors are USBHS1's at 58 and USBHS2's at 84
 * (the reference manual's vector table, the EVT's startup and IRQn, device-
 * data). Which line feeds which is not settled: the reference manual has
 * USBHS1 on 18 and USBHS2 on 20, the EVT's ch32v4x7_exti.h the other way
 * round. Either line enables both vectors, so the callback comes whichever is
 * right; the names follow the reference manual. */
#define CH32RV_EXTI_PVD 16u
#define CH32RV_EXTI_USBHS_WAKEUP 18u
#define CH32RV_EXTI_ETH_WAKEUP 19u
#define CH32RV_EXTI_USBHS2_WAKEUP 20u
#define CH32RV_EXTI_I3C_WAKEUP 21u
#define CH32RV_EXTI_LINE_VECTORS(X) \
    X(PVD, 1u << 16) \
    X(USBHS1WakeUp, (1u << 18) | (1u << 20)) \
    X(USBHS2Wakeup, (1u << 18) | (1u << 20)) \
    X(ETH_WKUP, 1u << 19) \
    X(I3CWakeup, 1u << 21)

#elif defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103)
#define CH32RV_EXTI_PVD 16u
#define CH32RV_EXTI_USBPD_WAKEUP 19u
#define CH32RV_EXTI_USBFS_WAKEUP 20u
#define CH32RV_EXTI_LPTIM_WAKEUP 21u
#define CH32RV_EXTI_COMP_WAKEUP 22u
#define CH32RV_EXTI_LINE_VECTORS(X) \
    X(PVD, 1u << 16) \
    X(USBPDWakeUp, 1u << 19) \
    X(USBFSWakeUp, 1u << 20) \
    X(LPTIMWakeUp, 1u << 21) \
    X(CMPWakeUp, 1u << 22)

#elif defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
#define CH32RV_EXTI_PVD 26u
#define CH32RV_EXTI_USBFS_WAKEUP 28u
#define CH32RV_EXTI_USBPD_WAKEUP 29u
#define CH32RV_EXTI_LINE_VECTORS(X) \
    X(PVD, 1u << 26) \
    X(USBFSWakeUp, 1u << 28) \
    X(USBPDWakeUp, 1u << 29)

#elif defined(CH32RV_SERIES_CH32M030)
/* Two USB PD ports: USBPD0 on 16 (CH32RV_EXTI_USBPD_WAKEUP), USBPD1 on 18.
 * No PVD. */
#define CH32RV_EXTI_USBPD_WAKEUP 16u
#define CH32RV_EXTI_USBPD1_WAKEUP 18u
#define CH32RV_EXTI_USBFS_WAKEUP 19u
#define CH32RV_EXTI_LINE_VECTORS(X) \
    X(USBPD0WakeUp, 1u << 16) \
    X(USBPD1WakeUp, 1u << 18) \
    X(USBFSWakeUp, 1u << 19)

#elif defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315)
#define CH32RV_EXTI_PVD 16u
#define CH32RV_EXTI_USBHS_WAKEUP 17u
#define CH32RV_EXTI_USBPD_WAKEUP 18u
#define CH32RV_EXTI_USBSS_WAKEUP 19u
#define CH32RV_EXTI_USART_WAKEUP 21u
#define CH32RV_EXTI_LINE_VECTORS(X) \
    X(PVD, 1u << 16) \
    X(USBHSWakeup, 1u << 17) \
    X(USBPDWakeUP, 1u << 18) \
    X(USBSSWakeup, 1u << 19) \
    X(USART_WKUP, 1u << 21)

#elif defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
/* No PVD line (the PVD has no interrupt here). Line 21 (ETH) is blank in the
 * reference manual's table and the EVT's header; 26 (USART) is the manual's
 * alone. */
#define CH32RV_EXTI_USBHS_WAKEUP 16u
#define CH32RV_EXTI_SWPMI_WAKEUP 18u
#define CH32RV_EXTI_USBFS_WAKEUP 19u
#define CH32RV_EXTI_USBPD_WAKEUP 20u
#define CH32RV_EXTI_ETH_WAKEUP 21u
#define CH32RV_EXTI_USBSS_WAKEUP 22u
#define CH32RV_EXTI_LPTIM_WAKEUP 23u
#define CH32RV_EXTI_LPTIM2_WAKEUP 24u
#define CH32RV_EXTI_I3C_WAKEUP 25u
#define CH32RV_EXTI_USART_WAKEUP 26u
#define CH32RV_EXTI_LINE_VECTORS(X) \
    X(USBHSWakeup, 1u << 16) \
    X(SWPMI_WKUP, 1u << 18) \
    X(USBFSWakeup, 1u << 19) \
    X(USBPDWakeUp, 1u << 20) \
    X(ETH_WKUP, 1u << 21) \
    X(USBSSWakeup, 1u << 22) \
    X(LPTIM1_WKUP, 1u << 23) \
    X(LPTIM2_WKUP, 1u << 24) \
    X(I3C_WKUP, 1u << 25) \
    X(USART_WKUP, 1u << 26)
#endif

/* The RTC alarm's line: its vector is ch32rv_rtc_alarm.c's, shared with
 * CH32RV.setAlarm() and the sleeps. */
#ifdef CH32RV_HAS_RTC
#define CH32RV_EXTI_RTC_ALARM CH32RV_RTC_ALARM_LINE
#define CH32RV_EXTI_RTC_MASK (1u << CH32RV_RTC_ALARM_LINE)
#else
#define CH32RV_EXTI_RTC_MASK 0u
#endif

#ifdef CH32RV_EXTI_LINE_VECTORS
#define CH32RV_EXTI_LINE_OR_(name, lines) | (lines)
/* Every line attachExtiLine() takes. */
#define CH32RV_EXTI_LINE_MASK (0u CH32RV_EXTI_LINE_VECTORS(CH32RV_EXTI_LINE_OR_) | CH32RV_EXTI_RTC_MASK)
/* Callbacks are kept for lines FIRST .. LAST: 1 on V00x, 11 on H41x. */
#define CH32RV_EXTI_LINE_FIRST ((uint8_t)__builtin_ctz(CH32RV_EXTI_LINE_MASK))
#define CH32RV_EXTI_LINE_LAST ((uint8_t)(31 - __builtin_clz(CH32RV_EXTI_LINE_MASK)))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The line's interrupt and wake (RTENR / FTENR from mode: RISING, FALLING or
 * CHANGE as PinStatus numbers them) with cb run in it (may be null: the line
 * only wakes). 0 for a line this series does not serve here or a level mode
 * (EXTI sees edges only). */
int ch32rv_exti_line_attach(uint8_t line, void (*cb)(void), int mode);
/* Off again. The RTC alarm's line stays on while setAlarm() or a sleep uses
 * it. */
void ch32rv_exti_line_detach(uint8_t line);
/* The callback attached to the line, or null. */
void (*ch32rv_exti_line_callback(uint8_t line))(void);
/* Raise the line from software (SWIEVR): its callback runs as on an edge. */
void ch32rv_exti_line_trigger(uint8_t line);
/* For a vector shared with another driver (the RTC alarm's): the lines it
 * found pending and cleared, to run their callbacks. */
void ch32rv_exti_line_deliver(uint32_t pending);

/* ch32rv_rtc_alarm.c: where its vector hands the RTC alarm line's edges, set
 * only by attachExtiLine() (null: the line is the alarm's and the sleeps'). */
extern void (*ch32rv_rtc_alarm_line_hook)(uint32_t pending);

#ifdef __cplusplus
}
#endif
