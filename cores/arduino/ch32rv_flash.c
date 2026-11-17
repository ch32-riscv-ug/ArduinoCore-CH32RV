/* See ch32rv_flash.h. */
#include "ch32rv_flash.h"

#include "internal/ch32rv_flash_ops.h"

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
    return CH32RV_FLASH_ERASE_SIZE;
}

static bool range_ok(uint32_t offset, uint32_t length, uint32_t unit)
{
    const uint32_t end = offset + length;
    if (((offset | length) & (unit - 1u)) || !length || end < offset ||
        end > ch32rv_flash_size()) {
        return false;
    }
#if !CH32RV_FLASH_SELF_WRITE
    if (offset < (uint32_t)(uintptr_t)_ch32rv_image_end) {
        return false;
    }
#endif
    return true;
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
