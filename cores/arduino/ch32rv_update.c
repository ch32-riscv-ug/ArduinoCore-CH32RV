/* See ch32rv_update.h. */
#include "ch32rv_update.h"

#include "ch32rv_crc.h"
#include "ch32rv_flash.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_irq.h"

#define PAGE CH32RV_FLASH_WRITE_SIZE
#define UNIT CH32RV_FLASH_ERASE_SIZE
/* The flash at its own address: 0x0 shows the system flash when the BOOT area started us. */
#define FLASH_AT(offset) ((const volatile uint32_t *)(uintptr_t)(0x08000000u + (offset)))

static struct {
    uint32_t base;          /* the staging area's flash offset */
    uint32_t size;
    uint32_t progress;
    uint32_t erased_to;     /* erased from the image's start up to here */
    uint32_t crc;
    uint32_t length;        /* the accepted image's */
    uint16_t fill;          /* bytes in page */
    uint8_t error;
    bool running, random, pending;
    uint32_t page[PAGE / 4u];
} u;

static bool fail(uint8_t error)
{
    u.error = error;
    u.running = false;
    return false;
}

static bool same(uint32_t offset, const uint32_t *page)
{
    const volatile uint32_t *f = FLASH_AT(offset);
    for (uint32_t i = 0; i < PAGE / 4u; i++) {
        if (f[i] != page[i]) {
            return false;
        }
    }
    return true;
}

static bool blank(uint32_t offset)
{
    const volatile uint32_t *f = FLASH_AT(offset);
    for (uint32_t i = 0; i < PAGE / 4u; i++) {
        if (f[i] != CH32RV_FLASH_ERASED_WORD) {
            return false;
        }
    }
    return true;
}

static void copy_in(const uint8_t *from, uint32_t at, uint32_t n)
{
    uint8_t *page = (uint8_t *)u.page;
    for (uint32_t i = 0; i < n; i++) {
        page[at + i] = from[i];
    }
}

/* Program u.page at `offset` of the image, erasing ahead, and read it back. */
static bool put_page(uint32_t offset)
{
    while (u.erased_to < offset + PAGE) {
        if (!ch32rv_flash_erase_page(u.base + u.erased_to)) {
            return fail(CH32RV_UPDATE_ERASE);
        }
        u.erased_to += UNIT;
    }
    if (!ch32rv_flash_write_page(u.base + offset, u.page) || !same(u.base + offset, u.page)) {
        return fail(CH32RV_UPDATE_WRITE);
    }
    if (offset == 0 && !ch32rv_flash_image_plausible(u.page[0], u.size)) {
        return fail(CH32RV_UPDATE_MAGIC_BYTE);
    }
    return true;
}

bool ch32rv_update_begin(uint32_t size, bool random)
{
    u.pending = false;
    u.running = false;
    u.error = CH32RV_UPDATE_OK;
    const uint32_t room = ch32rv_flash_staging_size();
    if (!room) {
        return fail(CH32RV_UPDATE_NO_SPACE);
    }
    if (size == CH32RV_UPDATE_SIZE_UNKNOWN && !random) {
        size = room;
    }
    if (!size || size == CH32RV_UPDATE_SIZE_UNKNOWN) {
        return fail(CH32RV_UPDATE_SIZE);
    }
    if (size > room) {
        return fail(CH32RV_UPDATE_SPACE);
    }
    u.base = ch32rv_flash_staging_offset();
    u.size = size;
    u.progress = 0;
    u.erased_to = 0;
    u.fill = 0;
    u.crc = CH32RV_CRC32_INIT;
    u.random = random;
    if (random) {
        /* Out-of-order pieces cannot erase around themselves: all of it now. */
        const uint32_t span = (size + UNIT - 1u) & ~(UNIT - 1u);
        for (; u.erased_to < span; u.erased_to += UNIT) {
            if (!ch32rv_flash_erase_page(u.base + u.erased_to)) {
                return fail(CH32RV_UPDATE_ERASE);
            }
        }
    }
    u.running = true;
    return true;
}

bool ch32rv_update_write(const void *data, uint32_t len)
{
    if (!u.running || u.random) {
        return false;
    }
    if (len > u.size - u.progress) {
        return fail(CH32RV_UPDATE_SPACE);
    }
    const uint8_t *src = (const uint8_t *)data;
    u.crc = ch32rv_crc32_update(u.crc, src, len);
    for (uint32_t done = 0; done < len;) {
        uint32_t n = PAGE - u.fill;
        if (n > len - done) {
            n = len - done;
        }
        copy_in(src + done, u.fill, n);
        u.fill = (uint16_t)(u.fill + n);
        done += n;
        u.progress += n;
        if (u.fill == PAGE) {
            if (!put_page(u.progress - PAGE)) {
                return false;
            }
            u.fill = 0;
        }
    }
    return true;
}

bool ch32rv_update_write_at(uint32_t offset, const void *data, uint32_t len)
{
    if (!u.running || !u.random) {
        return false;
    }
    if ((offset | len) & (PAGE - 1u)) {
        return fail(CH32RV_UPDATE_BAD_ARGUMENT);
    }
    if (offset + len > u.size || offset + len < offset) {
        return fail(CH32RV_UPDATE_SPACE);
    }
    const uint8_t *src = (const uint8_t *)data;
    for (uint32_t done = 0; done < len; done += PAGE) {
        const uint32_t at = offset + done;
        copy_in(src + done, 0, PAGE);
        if (!blank(u.base + at)) {
            if (!same(u.base + at, u.page)) {       /* not just the same piece again */
                return fail(CH32RV_UPDATE_WRITE);
            }
            continue;
        }
        if (!put_page(at)) {
            return false;
        }
        u.progress += PAGE;
    }
    return true;
}

bool ch32rv_update_end(bool even_if_remaining)
{
    if (!u.running) {
        return false;
    }
    if (!u.random && u.progress != u.size) {
        if (!even_if_remaining || !u.progress) {
            return fail(CH32RV_UPDATE_ABORT);
        }
        u.size = u.progress;
    }
    if (!u.random && u.fill) {
        uint8_t *page = (uint8_t *)u.page;
        for (uint32_t i = u.fill; i < PAGE; i++) {
            page[i] = 0xFF;
        }
        if (!put_page(u.progress - u.fill)) {
            return false;
        }
        u.fill = 0;
    }
    u.running = false;
    if (!ch32rv_flash_image_plausible(FLASH_AT(u.base)[0], u.size)) {
        return fail(CH32RV_UPDATE_MAGIC_BYTE);
    }
    /* What is in the flash is what was given, all of it in its place. */
    const uint32_t staged = ch32rv_crc32((const void *)FLASH_AT(u.base), u.size);
    if (!u.random && staged != u.crc) {
        return fail(CH32RV_UPDATE_VERIFY);
    }
    u.crc = staged;
    u.length = u.size;
    u.pending = true;
    return true;
}

void ch32rv_update_abort(void)
{
    u.pending = false;
    fail(CH32RV_UPDATE_ABORT);
}

uint8_t ch32rv_update_error(void)
{
    return u.error;
}

void ch32rv_update_clear_error(void)
{
    u.error = CH32RV_UPDATE_OK;
}

bool ch32rv_update_running(void)
{
    return u.running;
}

uint32_t ch32rv_update_size(void)
{
    return u.size;
}

uint32_t ch32rv_update_progress(void)
{
    return u.progress;
}

bool ch32rv_update_pending(void)
{
    return u.pending;
}

void ch32rv_update_cancel_pending(void)
{
    u.pending = false;
}

uint32_t ch32rv_update_crc32(void)
{
    return u.crc;
}

void ch32rv_update_restart(void)
{
    /* The core's global interrupt switch: the sketch runs in U mode on most
     * QingKe cores, where mstatus is an illegal instruction (mcause 2 at this
     * very line on CH32V205, 2026-11-17: the first DFU install never reset). */
    ch32rv_irq_disable_all();
    if (u.pending) {
        ch32rv_flash_install(u.base, u.length);
    }
    for (;;) {
        CH32RV_SYSTEM_RESET();
    }
}
