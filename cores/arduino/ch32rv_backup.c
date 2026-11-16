/* The sketch's backup words (CH32RV.getBackupRegister() and the rest,
 * ch32rv_rtc.h): BKP DR5 and up, or a .noinit record where there are no BKP
 * data registers (internal/ch32rv_backup.h). A file of its own, so that only
 * a sketch that uses them carries them. */
#include "Arduino.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_rtc.h"
#include "internal/ch32rv_backup.h"

#ifdef CH32RV_HAS_RTC

#define FIRST_USER 5u                /* DR1-DR4 are the core's */

#ifdef CH32RV_RTC_STORE_RAM

/* volatile, and read through a function the compiler cannot see through:
 * as the deepSleep() record (ch32rv_power.c), clang would otherwise take a
 * .noinit static for the zero it never is. */
volatile struct ch32rv_rtc_store ch32rv_rtc_store __attribute__((section(".noinit")));

static uint16_t store_sum(void)
{
    uint16_t sum = 0x5A3Cu;
    for (uint8_t i = 1; i <= 4u; i++) {
        sum = (uint16_t)((sum << 1 | sum >> 15) ^ ch32rv_rtc_store.dr[i]);
    }
    for (uint8_t i = 0; i < CH32RV_BACKUP_WORDS; i++) {
        sum = (uint16_t)((sum << 1 | sum >> 15) ^ ch32rv_rtc_store.user[i]);
    }
    return sum;
}

void ch32rv_rtc_domain_seal(void)
{
    ch32rv_rtc_store.check = store_sum();
}

void ch32rv_rtc_domain_open(void)
{
    ch32rv_clock_enable(PWR);
#ifndef CH32RV_RTC_X3X5
    /* H41x: the RTC's clock bits are in the backup domain (RCC_BDCTLR). */
    ch32rv_clock_enable(BKP);
    CH32RV_PWR_CTLR_REG |= 1u << 8;          /* DBP */
#endif
    if (ch32rv_rtc_store.check != store_sum()) {
        /* RAM after power-up: nothing in it is ours. */
        for (uint8_t i = 0; i < 5u; i++) {
            ch32rv_rtc_store.dr[i] = 0;
        }
        for (uint8_t i = 0; i < CH32RV_BACKUP_WORDS; i++) {
            ch32rv_rtc_store.user[i] = 0;
        }
        ch32rv_rtc_domain_seal();
    }
}

uint8_t ch32rv_backup_count(void)
{
    return (uint8_t)CH32RV_BACKUP_WORDS;
}

#else

uint8_t ch32rv_backup_count(void)
{
#if defined(CH32RV_SERIES_CH32V103) || defined(CH32RV_SERIES_CH32V205) || \
    defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103)
    return 10u - (FIRST_USER - 1u);          /* ten, their reference manuals say */
#else
    /* V20x, V30x, V4x7: "up to 42" (the reference manuals and datasheets do
     * not say which parts have fewer), so the part answers: DR42 keeps a
     * pattern where it exists. Its value is put back. */
    static uint8_t count;
    if (!count) {
        ch32rv_rtc_domain_open();
        const uint16_t was = CH32RV_RTC_DR(42u);
        CH32RV_RTC_DR(42u) = (uint16_t)(was ^ 0xA55Au);
        const int there = CH32RV_RTC_DR(42u) == (uint16_t)(was ^ 0xA55Au);
        CH32RV_RTC_DR(42u) = was;
        count = (uint8_t)((there ? 42u : 10u) - (FIRST_USER - 1u));
    }
    return count;
#endif
}

#endif

uint16_t ch32rv_backup_read(uint8_t index)
{
    if (index >= ch32rv_backup_count()) {
        return 0;
    }
    ch32rv_rtc_domain_open();
    return CH32RV_RTC_DR(FIRST_USER + (uint32_t)index);
}

int ch32rv_backup_write(uint8_t index, uint16_t value)
{
    if (index >= ch32rv_backup_count()) {
        return 0;
    }
    ch32rv_rtc_domain_open();
    CH32RV_RTC_DR(FIRST_USER + (uint32_t)index) = value;
    ch32rv_rtc_domain_seal();
    return 1;
}

#endif
