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

#ifdef CH32RV_FLASH_PAGE_SIZE

#include "ch32rv_registers.h"

#ifndef FLASH_MEM
#define KEY1 0x45670123u
#define KEY2 0xCDEF89ABu
#define FLASH_MEM 0x08000000u
#endif

/* V003, V00x, M030, V103, L103, X035 and V205 fill a page buffer, LOAD words at
 * a time (EVT FLASH_BufLoad), then start; V20x and V30x take the words straight
 * in (EVT FLASH_ProgramPage_Fast). */
#if defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V303) || \
    defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317)
#define PG_BUF 0
#else
#define PG_BUF 1
#endif
#if defined(CH32RV_SERIES_CH32V103)
#define PG_LOAD 4u
#elif defined(CH32RV_SERIES_CH32M030)
#define PG_LOAD 2u
#else
#define PG_LOAD 1u
#endif

#define PG_KEYR     CH32RV_REG32(CH32RV_FLASH_BASE + 0x04u)
#define PG_STATR    CH32RV_REG32(CH32RV_FLASH_BASE + 0x0Cu)
#define PG_CTLR     CH32RV_REG32(CH32RV_FLASH_BASE + 0x10u)
#define PG_ADDR     CH32RV_REG32(CH32RV_FLASH_BASE + 0x14u)
#define PG_MODEKEYR CH32RV_REG32(CH32RV_FLASH_BASE + 0x24u)

#define PG_PAGE_PG  (1u << 16)
#define PG_PAGE_ER  (1u << 17)
#define PG_BUF_LOAD (1u << 18)
#define PG_BUF_RST  (1u << 19)
#define PG_PG_STRT  (1u << 21)
#define PG_STRT     (1u << 6)
#define PG_LOCKS    ((1u << 7) | (1u << 15))
#define PG_ERRS     0x30u /* EOP, WRPRTERR */

extern uint8_t _ch32rv_store_start[], _ch32rv_store_end[];

/* Wait for BSY to clear; false on WRPRTERR or when it never clears (a page
 * takes a few ms; this gives up after about a second at the slowest clock). */
static bool page_done(void)
{
    for (uint32_t n = 0x400000u; n; n--) {
        const uint32_t s = PG_STATR;
        if (s & 0x10u) {
            return false;
        }
        if (!(s & 1u)) {
            return true;
        }
    }
    return false;
}

bool ch32rv_flash_write_page(const void *page, const uint32_t *data)
{
    const uint8_t *p8 = (const uint8_t *)page;
    if (p8 < _ch32rv_store_start || p8 >= _ch32rv_store_end || ((uintptr_t)p8 & (CH32RV_FLASH_PAGE_SIZE - 1u))) {
        return false;
    }
    const uint32_t a = (uintptr_t)p8 | FLASH_MEM;
    volatile uint32_t *dst = (volatile uint32_t *)a;
    PG_KEYR = KEY1;
    PG_KEYR = KEY2;
    PG_MODEKEYR = KEY1;
    PG_MODEKEYR = KEY2;
    PG_STATR = PG_ERRS;
    PG_CTLR = PG_PAGE_ER;
    PG_ADDR = a;
    PG_CTLR = PG_PAGE_ER | PG_STRT;
    bool ok = page_done();
#if PG_BUF
    PG_CTLR = PG_PAGE_PG;
    PG_CTLR = PG_PAGE_PG | PG_BUF_RST;
    ok = ok && page_done();
    for (uint32_t i = 0; i < CH32RV_FLASH_PAGE_SIZE / 4u && ok; i += PG_LOAD) {
        for (uint32_t k = 0; k < PG_LOAD; k++) {
            dst[i + k] = data[i + k];
        }
        PG_CTLR = PG_PAGE_PG | PG_BUF_LOAD;
        ok = page_done();
    }
    PG_ADDR = a;
    PG_CTLR = PG_PAGE_PG | PG_STRT;
#else
    PG_CTLR = PG_PAGE_PG;
    ok = ok && page_done();
    while (PG_STATR & 2u) {
    }
    for (uint32_t i = 0; i < CH32RV_FLASH_PAGE_SIZE / 4u; i++) {
        dst[i] = data[i];
        while (PG_STATR & 2u) {
        }
    }
    PG_CTLR = PG_PAGE_PG | PG_PG_STRT;
#endif
    ok = page_done() && ok;
    PG_CTLR = 0;
    PG_STATR = PG_ERRS;
    PG_CTLR = PG_LOCKS;
    return ok;
}

#endif
