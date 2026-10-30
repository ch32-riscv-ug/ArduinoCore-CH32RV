/* Menus kept in the USER option byte, set at boot to what the image was built
 * for: the option bytes stay as they are across uploads, so the build has to
 * say what it wants, and the core makes the chip so before the sketch runs.
 *
 * - Flash / RAM Split of CH32V303/V307/V317 (RC, VC, WC, SC): the chip's
 *   320 KB of SRAM holds both the copy of flash the code runs from without
 *   wait states and the RAM, divided by bits [7:5] of USER (RM 32.4.6, Table
 *   32-4). The linker script is built for the menu's split, so the chip has
 *   to be set the same way before anything touches RAM past 32 KB - the stack
 *   sits at its top.
 * - Reset Pin of the families whose reset input is an option (V00x and M007,
 *   X033/X035, M030, X305/X315): USER [4:3] RST_MODE, X3x5's bit 3 CFGRSTEN.
 *   The reset pin or a GPIO, each series defaulting to its factory value.
 *
 * crt0 calls these before .data and .bss: no globals here. When the value
 * loaded at reset (FLASH_OBR) is another one, the option bytes are rewritten -
 * all eight halfwords as they were, USER's bits changed and its complement
 * with them - and the chip resets, which loads them. A split that cannot be
 * set stops (a loop a debugger can halt): when the option bytes already hold
 * it and the chip still runs another (128 KB + 192 KB on a lot without it),
 * and when the flash is read-protected. A reset pin that cannot be set is
 * left as it is and the sketch runs. */
#include <stdint.h>

#if defined(CH32RV_RAMSPLIT_USER) || defined(CH32RV_RSTPIN_USER)

#include "ch32rv_registers.h"

#define FLASH_KEYR   CH32RV_REG32(CH32RV_FLASH_BASE + 0x04u)
#define FLASH_OBKEYR CH32RV_REG32(CH32RV_FLASH_BASE + 0x08u)
#define FLASH_STATR  CH32RV_REG32(CH32RV_FLASH_BASE + 0x0Cu)
#define FLASH_CTLR   CH32RV_REG32(CH32RV_FLASH_BASE + 0x10u)
#define FLASH_OBR    CH32RV_REG32(CH32RV_FLASH_BASE + 0x1Cu)

#define KEY1     0x45670123u
#define KEY2     0xCDEF89ABu
#define CR_OPTPG 0x10u
#define CR_OPTER 0x20u
#define CR_STRT  0x40u
#define CR_LOCK  0x80u
#define SR_BSY   0x01u
#define OBR_RDPRT 0x02u

#define OPTION_BYTES ((volatile uint16_t *)0x1FFFF800u)

static void stop(void)
{
    for (;;) {
    }
}

static void wait_ready(void)
{
    while (FLASH_STATR & SR_BSY) {
    }
}

/* USER as loaded at reset. */
static uint32_t loaded_user(void)
{
    return FLASH_OBR >> 2 & 0xFFu;
}

/* 0: nothing to do; 1: rewrite; -1: it cannot be set (the option bytes
 * already hold it, or the flash is read-protected). */
static int needs(uint32_t value, uint32_t mask)
{
    if (((loaded_user() ^ value) & mask) == 0) {
        return 0;
    }
    if (((OPTION_BYTES[1] ^ value) & mask) == 0 || (FLASH_OBR & OBR_RDPRT)) {
        return -1;
    }
    return 1;
}

/* Erase the option bytes, write them back with USER's `mask` bits set to
 * `value`, and reset. Does not return. */
static void rewrite(uint32_t value, uint32_t mask)
{
    volatile uint16_t *const ob = OPTION_BYTES;
    uint16_t v[8];
    for (int i = 0; i < 8; i++) {
        v[i] = ob[i];
    }
    const uint32_t user = ((v[1] & 0xFFu) & ~mask) | value;
    v[1] = (uint16_t)(user | (~user & 0xFFu) << 8);

    FLASH_KEYR = KEY1;
    FLASH_KEYR = KEY2;
    FLASH_OBKEYR = KEY1;
    FLASH_OBKEYR = KEY2;
    wait_ready();
    FLASH_CTLR |= CR_OPTER;
    FLASH_CTLR |= CR_STRT;
    wait_ready();
    FLASH_CTLR &= ~CR_OPTER;
    FLASH_CTLR |= CR_OPTPG;
    for (int i = 0; i < 8; i++) {
        ob[i] = v[i];
        wait_ready();
    }
    FLASH_CTLR &= ~CR_OPTPG;
    FLASH_CTLR |= CR_LOCK;

    CH32RV_PFIC_CFGR = CH32RV_PFIC_KEY3 | CH32RV_PFIC_SYSRST; /* the option bytes load at the reset */
    stop();
}

#if defined(CH32RV_RAMSPLIT_USER)
__attribute__((used, noinline)) void ch32rv_ramsplit_check(void)
{
    const int n = needs(CH32RV_RAMSPLIT_USER, CH32RV_RAMSPLIT_MASK);
    if (n < 0) {
        stop();
    }
    if (n > 0) {
        rewrite(CH32RV_RAMSPLIT_USER, CH32RV_RAMSPLIT_MASK);
    }
}
#endif

#if defined(CH32RV_RSTPIN_USER)
__attribute__((used, noinline)) void ch32rv_rstpin_check(void)
{
    if (needs(CH32RV_RSTPIN_USER, CH32RV_RSTPIN_MASK) > 0) {
        rewrite(CH32RV_RSTPIN_USER, CH32RV_RSTPIN_MASK);
    }
}
#endif

#endif
