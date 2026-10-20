/* See ch32rv_flash.h. */
#include "ch32rv_flash.h"

#if CH32RV_FLASH_SELF_WRITE

#include <string.h>

#include "ch32rv_registers.h"

#define FL_KEYR     CH32RV_REG32(CH32RV_FLASH_BASE + 0x04u)
#define FL_STATR    CH32RV_REG32(CH32RV_FLASH_BASE + 0x0Cu)
#define FL_CTLR     CH32RV_REG32(CH32RV_FLASH_BASE + 0x10u)
#define FL_ADDR     CH32RV_REG32(CH32RV_FLASH_BASE + 0x14u)
#define FL_MODEKEYR CH32RV_REG32(CH32RV_FLASH_BASE + 0x24u)
#define FL_CFGR0    CH32RV_REG32(CH32RV_FLASH_BASE + 0x2Cu)

#define KEY1 0x45670123u
#define KEY2 0xCDEF89ABu

#define CR_PER      0x00000002u
#define CR_STRT     0x00000040u
#define CR_LOCK     0x00000080u
#define CR_FLOCK    0x00008000u
#define CR_PAGE_PG  0x00010000u
#define CR_PG_STRT  0x00200000u
#define SR_BSY      0x00000001u
#define SR_WR_BSY   0x00000002u
#define SR_WRPRTERR 0x00000010u
#define SR_EOP      0x00000020u
#define CFGR0_DBMODE (1u << 28)

/* Programmed through the flash's own address; offset 0 is what 0x0 aliases. */
#define FLASH_MEM 0x08000000u

uint32_t ch32rv_flash_size(void)
{
    return (FL_CFGR0 & CFGR0_DBMODE) ? 960u * 1024u : 480u * 1024u;
}

uint32_t ch32rv_flash_erase_size(void)
{
    return (FL_CFGR0 & CFGR0_DBMODE) ? 8192u : 4096u;
}

static void unlock(void)
{
    FL_KEYR = KEY1;
    FL_KEYR = KEY2;
    FL_MODEKEYR = KEY1;
    FL_MODEKEYR = KEY2;
}

static void lock(void)
{
    FL_CTLR |= CR_FLOCK;
    FL_CTLR |= CR_LOCK;
}

static bool done_ok(void)
{
    while (FL_STATR & SR_BSY) {
    }
    const uint32_t st = FL_STATR;
    FL_STATR = SR_EOP | SR_WRPRTERR;
    return !(st & SR_WRPRTERR);
}

bool ch32rv_flash_erase(uint32_t offset, uint32_t length)
{
    const uint32_t unit = ch32rv_flash_erase_size();
    if ((offset | length) & (unit - 1u) || offset + length > ch32rv_flash_size() || offset + length < offset) {
        return false;
    }
    bool ok = true;
    unlock();
    for (uint32_t a = offset; a < offset + length && ok; a += unit) {
        FL_CTLR |= CR_PER;
        FL_ADDR = FLASH_MEM + a;
        FL_CTLR |= CR_STRT;
        ok = done_ok();
        FL_CTLR &= ~CR_PER;
    }
    lock();
    return ok;
}

bool ch32rv_flash_write(uint32_t offset, const void *data, uint32_t length)
{
    if ((offset | length) & (CH32RV_FLASH_WRITE_SIZE - 1u) || offset + length > ch32rv_flash_size() ||
        offset + length < offset) {
        return false;
    }
    const uint8_t *src = (const uint8_t *)data;
    bool ok = true;
    unlock();
    for (uint32_t a = offset; a < offset + length && ok; a += CH32RV_FLASH_WRITE_SIZE) {
        FL_CTLR |= CR_PAGE_PG;
        while (FL_STATR & (SR_BSY | SR_WR_BSY)) {
        }
        volatile uint32_t *dst = (volatile uint32_t *)(FLASH_MEM + a);
        for (uint32_t i = 0; i < CH32RV_FLASH_WRITE_SIZE / 4u; i++) {
            uint32_t w;
            memcpy(&w, src, 4);
            src += 4;
            dst[i] = w;
            while (FL_STATR & SR_WR_BSY) {
            }
        }
        FL_CTLR |= CR_PG_STRT;
        ok = done_ok();
        FL_CTLR &= ~CR_PAGE_PG;
    }
    lock();
    return ok;
}

#endif
