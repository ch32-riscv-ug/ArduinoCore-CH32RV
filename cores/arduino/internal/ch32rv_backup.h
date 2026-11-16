/* The backup words, shared by the RTC (ch32rv_rtc.c, DR1-DR4) and the
 * sketch's (ch32rv_backup.c, DR5 and up): where they live and how to reach
 * them. Internal to the core.
 *
 *   - BKP families (V103, V20x, V205, V30x, V4x7, L103 / M103): the BKP data
 *     registers, behind the PWR and BKP clocks and PWR_CTLR.DBP. 10 on V103,
 *     V205 and L103 / M103 (their reference manuals); "up to 42" on V20x, V30x
 *     and V4x7, so the count is read from the part (ch32rv_backup.c).
 *   - H41x, X305 / X315 (no BKP data registers): one .noinit record with a
 *     check word, so RAM's power-on contents never pass for it. Kept through
 *     every reset that leaves RAM powered, lost with power. */
#pragma once

#include <stdint.h>
#include "ch32rv_registers.h"
#include "ch32rv_rtc.h"

#ifdef CH32RV_HAS_RTC

#define CH32RV_PWR_CTLR_REG  CH32RV_REG32(0x40007000u)

#ifdef CH32RV_RTC_STORE_RAM

struct ch32rv_rtc_store {
    uint16_t dr[5];                          /* [1..4]: the core's, as DR1-DR4 */
    uint16_t user[CH32RV_BACKUP_WORDS];      /* the sketch's */
    uint16_t check;
};
extern volatile struct ch32rv_rtc_store ch32rv_rtc_store;
#define CH32RV_RTC_DR(n) \
    (*((n) <= 4u ? &ch32rv_rtc_store.dr[(n)] : &ch32rv_rtc_store.user[(n) - 5u]))

#ifdef __cplusplus
extern "C" {
#endif
/* Zeroes the record when its check word does not match (RAM after power-up). */
void ch32rv_rtc_domain_open(void);
void ch32rv_rtc_domain_seal(void);
#ifdef __cplusplus
}
#endif

#else

/* DR1-DR10 at 0x04-0x28, DR11-DR42 at 0x40-0xBC. */
#define CH32RV_RTC_DR(n) \
    CH32RV_REG16((n) <= 10u ? 0x40006C00u + 4u * (n) : 0x40006C40u + 4u * ((n) - 11u))

static inline void ch32rv_rtc_domain_open(void)
{
    ch32rv_clock_enable(PWR);
    ch32rv_clock_enable(BKP);
    CH32RV_PWR_CTLR_REG |= 1u << 8;          /* DBP: the backup domain writable */
}

static inline void ch32rv_rtc_domain_seal(void)
{
}

#endif
#endif
