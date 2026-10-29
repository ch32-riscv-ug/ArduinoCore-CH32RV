/* The Flash / RAM split menu of CH32V303/V307/V317 (RC, VC, WC, SC): the
 * chip's 320 KB of SRAM holds both the copy of flash the code runs from
 * without wait states and the RAM, divided by bits [7:5] of the USER option
 * byte (RM 32.4.6, Table 32-4). The linker script is built for the menu's
 * split, so the chip has to be set the same way before anything touches RAM
 * past 32 KB - the stack sits at its top.
 *
 * crt0 calls this first, on a stack at the bottom of RAM, before .data and
 * .bss: no globals here. When the split loaded at reset (FLASH_OBR) is
 * another one, it rewrites the option bytes - all eight halfwords as they
 * were, USER's split bits changed - and resets the chip, which loads them.
 * It stops (a loop a debugger can halt) rather than rewrite again when the
 * option bytes already hold the split and the chip still runs another (128 KB
 * + 192 KB on a lot without it), and when the flash is read-protected. */
#include <stdint.h>

#if defined(CH32RV_RAMSPLIT_USER)

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

__attribute__((used, noinline)) void ch32rv_ramsplit_check(void)
{
    const uint32_t user = FLASH_OBR >> 2 & 0xFFu; /* USER as loaded at reset */
    if (((user ^ CH32RV_RAMSPLIT_USER) & CH32RV_RAMSPLIT_MASK) == 0) {
        return;
    }
    volatile uint16_t *const ob = (volatile uint16_t *)0x1FFFF800u;
    uint16_t v[8];
    for (int i = 0; i < 8; i++) {
        v[i] = ob[i];
    }
    if (((v[1] ^ CH32RV_RAMSPLIT_USER) & CH32RV_RAMSPLIT_MASK) == 0 || (FLASH_OBR & OBR_RDPRT)) {
        stop();
    }
    v[1] = (uint16_t)((v[1] & ~CH32RV_RAMSPLIT_MASK) | CH32RV_RAMSPLIT_USER);

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

#endif
