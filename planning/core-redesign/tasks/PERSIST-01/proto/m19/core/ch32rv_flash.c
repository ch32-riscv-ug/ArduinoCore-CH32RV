/* EXPERIMENT (PERSIST-01 M19): see ch32rv_flash.h. */
#include "ch32rv_flash.h"

#include <string.h>

#include "ch32rv_registers.h"

#ifndef FX_RMW
#define FX_RMW 1
#endif
#ifndef FX_CHECK
#define FX_CHECK 1
#endif
#ifndef FX_ALIGNED
#define FX_ALIGNED 0
#endif

#define FL_KEYR     CH32RV_REG32(CH32RV_FLASH_BASE + 0x04u)
#define FL_STATR    CH32RV_REG32(CH32RV_FLASH_BASE + 0x0Cu)
#define FL_CTLR     CH32RV_REG32(CH32RV_FLASH_BASE + 0x10u)
#define FL_ADDR     CH32RV_REG32(CH32RV_FLASH_BASE + 0x14u)
#define FL_MODEKEYR CH32RV_REG32(CH32RV_FLASH_BASE + 0x24u)
#define FL_CFGR0    CH32RV_REG32(CH32RV_FLASH_BASE + 0x2Cu)

#define KEY1 0x45670123u
#define KEY2 0xCDEF89ABu

#define CR_PER      (1u << 1)
#define CR_STRT     (1u << 6)
#define CR_LOCK     (1u << 7)
#define CR_FLOCK    (1u << 15)
#define CR_PAGE_PG  (1u << 16)
#define CR_PAGE_ER  (1u << 17)
#define CR_BUF_LOAD (1u << 18)
#define CR_BUF_RST  (1u << 19)
#define CR_PG_STRT  (1u << 21)
#define SR_BSY      (1u << 0)
#define SR_WR_BSY   (1u << 1)
#define SR_WRPRTERR (1u << 4)
#define SR_EOP      (1u << 5)

#define FLASH_MEM 0x08000000u

#if CH32RV_FLASH_ERASE_SIZE >= 4096u
#define CR_ER CR_PER /* no fast erase: the standard sector */
#else
#define CR_ER CR_PAGE_ER
#endif
#if defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V303) || \
    defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317) || \
    CH32RV_FLASH_ERASE_SIZE >= 4096u
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

#if FX_RMW
#define CR_SET(b) (FL_CTLR |= (b))
#define CR_CLR(b) (FL_CTLR &= ~(b))
#define CR_LOCKS() (FL_CTLR |= CR_FLOCK | CR_LOCK)
#else
#define CR_SET(b) (FL_CTLR = (b))
#define CR_CLR(b) (FL_CTLR = 0)
#define CR_LOCKS() (FL_CTLR = CR_FLOCK | CR_LOCK)
#endif
/* In the plain style a "set" replaces CTLR, so a second bit is set together with the first. */
#if FX_RMW
#define CR_SET2(a, b) CR_SET(b)
#else
#define CR_SET2(a, b) CR_SET((a) | (b))
#endif

extern uint8_t _ch32rv_flash_length[], _ch32rv_image_end[];

uint32_t ch32rv_flash_size(void)
{
#if CH32RV_FLASH_SELF_WRITE
    return (FL_CFGR0 & (1u << 28)) ? 960u * 1024u : 480u * 1024u;
#else
    return (uint32_t)(uintptr_t)_ch32rv_flash_length;
#endif
}

uint32_t ch32rv_flash_erase_size(void)
{
#if CH32RV_FLASH_SELF_WRITE
    return (FL_CFGR0 & (1u << 28)) ? 8192u : 4096u;
#else
    return CH32RV_FLASH_ERASE_SIZE;
#endif
}

static bool fl_range(uint32_t offset, uint32_t length, uint32_t unit)
{
#if FX_CHECK
    const uint32_t end = offset + length;
    if (((offset | length) & (unit - 1u)) || !length || end < offset ||
        end > (uint32_t)(uintptr_t)_ch32rv_flash_length) {
        return false;
    }
#if !CH32RV_FLASH_SELF_WRITE
    if (offset < (uint32_t)(uintptr_t)_ch32rv_image_end) { /* the running program */
        return false;
    }
#endif
#else
    (void)offset;
    (void)length;
    (void)unit;
#endif
    return true;
}

static void fl_unlock(void)
{
    FL_KEYR = KEY1;
    FL_KEYR = KEY2;
    FL_MODEKEYR = KEY1;
    FL_MODEKEYR = KEY2;
    FL_STATR = SR_EOP | SR_WRPRTERR;
}

static void fl_lock(void)
{
    FL_STATR = SR_EOP | SR_WRPRTERR;
    CR_LOCKS();
}

/* BSY clear: true; WRPRTERR or no end in sight: false. */
static bool fl_wait(void)
{
    for (uint32_t n = 0x400000u; n; n--) {
        const uint32_t s = FL_STATR;
        if (s & SR_WRPRTERR) {
            return false;
        }
        if (!(s & SR_BSY)) {
            return true;
        }
    }
    return false;
}

static bool fl_erase_one(uint32_t a)
{
    CR_SET(CR_ER);
    FL_ADDR = a;
    CR_SET2(CR_ER, CR_STRT);
    const bool ok = fl_wait();
    CR_CLR(CR_ER);
    return ok;
}

/* Program one write unit at a (FLASH_MEM based) from src; src word aligned when aligned. */
#ifndef FX_BYTES
#define FX_BYTES 1 /* unaligned words from four byte loads, not memcpy (newlib's is 298 B on rv32imac) */
#endif
static inline uint32_t fl_word(const uint8_t *p, bool aligned)
{
    if (aligned) {
        return *(const uint32_t *)p;
    }
#if FX_BYTES
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
#else
    uint32_t w;
    memcpy(&w, p, 4);
    return w;
#endif
}

static bool fl_write_one(uint32_t a, const uint8_t *src, bool aligned)
{
    volatile uint32_t *dst = (volatile uint32_t *)a;
    bool ok;
#if PG_BUF
    CR_SET(CR_PAGE_PG);
    CR_SET2(CR_PAGE_PG, CR_BUF_RST);
    ok = fl_wait();
    for (uint32_t i = 0; i < CH32RV_FLASH_WRITE_SIZE / 4u && ok; i += PG_LOAD) {
        for (uint32_t k = 0; k < PG_LOAD; k++) {
            dst[i + k] = fl_word(src + 4u * (i + k), aligned);
        }
        CR_SET2(CR_PAGE_PG, CR_BUF_LOAD);
        ok = fl_wait();
    }
    FL_ADDR = a;
    CR_SET2(CR_PAGE_PG, CR_STRT);
#else
    CR_SET(CR_PAGE_PG);
    ok = fl_wait();
    while (FL_STATR & SR_WR_BSY) {
    }
    for (uint32_t i = 0; i < CH32RV_FLASH_WRITE_SIZE / 4u; i++) {
        dst[i] = fl_word(src + 4u * i, aligned);
        while (FL_STATR & SR_WR_BSY) {
        }
    }
    CR_SET2(CR_PAGE_PG, CR_PG_STRT);
#endif
    ok = fl_wait() && ok;
    CR_CLR(CR_PAGE_PG);
    return ok;
}

bool ch32rv_flash_erase(uint32_t offset, uint32_t length)
{
    if (!fl_range(offset, length, CH32RV_FLASH_ERASE_SIZE)) {
        return false;
    }
    bool ok = true;
    fl_unlock();
    for (uint32_t a = offset; a < offset + length && ok; a += CH32RV_FLASH_ERASE_SIZE) {
        ok = fl_erase_one(FLASH_MEM | a);
    }
    fl_lock();
    return ok;
}

bool ch32rv_flash_write(uint32_t offset, const void *data, uint32_t length)
{
    if (!fl_range(offset, length, CH32RV_FLASH_WRITE_SIZE)) {
        return false;
    }
    const uint8_t *src = (const uint8_t *)data;
    bool ok = true;
    fl_unlock();
    for (uint32_t a = offset; a < offset + length && ok; a += CH32RV_FLASH_WRITE_SIZE) {
        ok = fl_write_one(FLASH_MEM | a, src, FX_ALIGNED || !((uintptr_t)src & 3u));
        src += CH32RV_FLASH_WRITE_SIZE;
    }
    fl_lock();
    return ok;
}

bool ch32rv_flash_erase_page(uint32_t offset)
{
    if (!fl_range(offset, CH32RV_FLASH_ERASE_SIZE, CH32RV_FLASH_ERASE_SIZE)) {
        return false;
    }
    fl_unlock();
    const bool ok = fl_erase_one(FLASH_MEM | offset);
    fl_lock();
    return ok;
}

bool ch32rv_flash_write_page(uint32_t offset, const uint32_t *data)
{
    if (!fl_range(offset, CH32RV_FLASH_WRITE_SIZE, CH32RV_FLASH_WRITE_SIZE)) {
        return false;
    }
    fl_unlock();
    const bool ok = fl_write_one(FLASH_MEM | offset, (const uint8_t *)data, true);
    fl_lock();
    return ok;
}

bool ch32rv_flash_rewrite_page(uint32_t offset, const uint32_t *data)
{
    if (!fl_range(offset, CH32RV_FLASH_ERASE_SIZE, CH32RV_FLASH_ERASE_SIZE)) {
        return false;
    }
    fl_unlock();
    const bool ok = fl_erase_one(FLASH_MEM | offset) && fl_write_one(FLASH_MEM | offset, (const uint8_t *)data, true);
    fl_lock();
    return ok;
}
