/* See ch32rv_flash.h. */
#include "ch32rv_flash.h"

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
#define CR_PAGE_ER  0x00020000u
#define CR_BUF_LOAD 0x00040000u
#define CR_BUF_RST  0x00080000u
#define CR_PG_STRT  0x00200000u
#define SR_BSY      0x00000001u
#define SR_WR_BSY   0x00000002u
#define SR_WRPRTERR 0x00000010u
#define SR_EOP      0x00000020u
#define CFGR0_DBMODE (1u << 28)

/* Programmed through the flash's own address; offset 0 is what 0x0 aliases. */
#define FLASH_MEM 0x08000000u

/* The fast (page) erase, or the standard sector erase where there is none. */
#if CH32RV_FLASH_ERASE_SIZE >= 4096u
#define CR_ER CR_PER
#else
#define CR_ER CR_PAGE_ER
#endif

/* V20x, V30x, V4x7, X3x5 and H41x take the words straight in, then PG_STRT;
 * the others fill a page buffer, PG_LOAD words per BUF_LOAD, then STRT. */
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

/* From the linker script: LENGTH(FLASH), and where the image ends in flash. */
extern uint8_t _ch32rv_flash_length[], _ch32rv_image_end[];

uint32_t ch32rv_flash_size(void)
{
#if CH32RV_FLASH_SELF_WRITE
    return (FL_CFGR0 & CFGR0_DBMODE) ? 960u * 1024u : 480u * 1024u;
#else
    return (uint32_t)(uintptr_t)_ch32rv_flash_length;
#endif
}

uint32_t ch32rv_flash_erase_size(void)
{
#if CH32RV_FLASH_SELF_WRITE
    return (FL_CFGR0 & CFGR0_DBMODE) ? 8192u : 4096u;
#else
    return CH32RV_FLASH_ERASE_SIZE;
#endif
}

static bool range_ok(uint32_t offset, uint32_t length, uint32_t unit)
{
    const uint32_t end = offset + length;
    if (((offset | length) & (unit - 1u)) || !length || end < offset ||
        end > (uint32_t)(uintptr_t)_ch32rv_flash_length) {
        return false;
    }
#if !CH32RV_FLASH_SELF_WRITE
    if (offset < (uint32_t)(uintptr_t)_ch32rv_image_end) {
        return false;
    }
#endif
    return true;
}

static void unlock(void)
{
    FL_KEYR = KEY1;
    FL_KEYR = KEY2;
    FL_MODEKEYR = KEY1;
    FL_MODEKEYR = KEY2;
    FL_STATR = SR_EOP | SR_WRPRTERR;
}

/* CTLR is only ever changed bit by bit: on V4x7 it also holds the flash
 * clock divider (SCKMOD) the clock setup chose. */
static void lock(void)
{
    FL_STATR = SR_EOP | SR_WRPRTERR;
    FL_CTLR |= CR_FLOCK | CR_LOCK;
}

/* BSY cleared: true. WRPRTERR, or still busy after ~0x400000 polls (far
 * beyond the slowest unit at the slowest clock): false. */
static bool done_ok(void)
{
    for (uint32_t n = 0x400000u; n; n--) {
        const uint32_t st = FL_STATR;
        if (st & SR_WRPRTERR) {
            return false;
        }
        if (!(st & SR_BSY)) {
            return true;
        }
    }
    return false;
}

static bool erase_one(uint32_t a)
{
    FL_CTLR |= CR_ER;
    FL_ADDR = a;
    FL_CTLR |= CR_STRT;
    const bool ok = done_ok();
    FL_CTLR &= ~CR_ER;
    return ok;
}

/* Byte loads when unaligned: memcpy would link newlib's (298 B on rv32imac). */
static inline uint32_t load_word(const uint8_t *p, bool aligned)
{
    if (aligned) {
        return *(const uint32_t *)p;
    }
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool write_one(uint32_t a, const uint8_t *src, bool aligned)
{
    volatile uint32_t *dst = (volatile uint32_t *)a;
    bool ok;
#if PG_BUF
    FL_CTLR |= CR_PAGE_PG;
    FL_CTLR |= CR_BUF_RST;
    ok = done_ok();
    for (uint32_t i = 0; i < CH32RV_FLASH_WRITE_SIZE / 4u && ok; i += PG_LOAD) {
        for (uint32_t k = 0; k < PG_LOAD; k++) {
            dst[i + k] = load_word(src + 4u * (i + k), aligned);
        }
        FL_CTLR |= CR_BUF_LOAD;
        ok = done_ok();
    }
    FL_ADDR = a;
    FL_CTLR |= CR_STRT;
#else
    FL_CTLR |= CR_PAGE_PG;
    ok = done_ok();
    while (FL_STATR & SR_WR_BSY) {
    }
    for (uint32_t i = 0; i < CH32RV_FLASH_WRITE_SIZE / 4u; i++) {
        dst[i] = load_word(src + 4u * i, aligned);
        while (FL_STATR & SR_WR_BSY) {
        }
    }
    FL_CTLR |= CR_PG_STRT;
#endif
    ok = done_ok() && ok;
    FL_CTLR &= ~CR_PAGE_PG;
    return ok;
}

bool ch32rv_flash_erase(uint32_t offset, uint32_t length)
{
    if (!range_ok(offset, length, CH32RV_FLASH_ERASE_SIZE)) {
        return false;
    }
    bool ok = true;
    unlock();
    for (uint32_t a = offset; a < offset + length && ok; a += CH32RV_FLASH_ERASE_SIZE) {
        ok = erase_one(FLASH_MEM | a);
    }
    lock();
    return ok;
}

bool ch32rv_flash_write(uint32_t offset, const void *data, uint32_t length)
{
    if (!range_ok(offset, length, CH32RV_FLASH_WRITE_SIZE)) {
        return false;
    }
    const uint8_t *src = (const uint8_t *)data;
    const bool aligned = !((uintptr_t)src & 3u);
    bool ok = true;
    unlock();
    for (uint32_t a = offset; a < offset + length && ok; a += CH32RV_FLASH_WRITE_SIZE) {
        ok = write_one(FLASH_MEM | a, src, aligned);
        src += CH32RV_FLASH_WRITE_SIZE;
    }
    lock();
    return ok;
}

bool ch32rv_flash_erase_page(uint32_t offset)
{
    if (!range_ok(offset, CH32RV_FLASH_ERASE_SIZE, CH32RV_FLASH_ERASE_SIZE)) {
        return false;
    }
    unlock();
    const bool ok = erase_one(FLASH_MEM | offset);
    lock();
    return ok;
}

bool ch32rv_flash_write_page(uint32_t offset, const uint32_t *data)
{
    if (!range_ok(offset, CH32RV_FLASH_WRITE_SIZE, CH32RV_FLASH_WRITE_SIZE)) {
        return false;
    }
    unlock();
    const bool ok = write_one(FLASH_MEM | offset, (const uint8_t *)data, true);
    lock();
    return ok;
}

bool ch32rv_flash_rewrite_page(uint32_t offset, const uint32_t *data)
{
    if (!range_ok(offset, CH32RV_FLASH_ERASE_SIZE, CH32RV_FLASH_ERASE_SIZE)) {
        return false;
    }
    unlock();
    const bool ok = erase_one(FLASH_MEM | offset) && write_one(FLASH_MEM | offset, (const uint8_t *)data, true);
    lock();
    return ok;
}
