/* Staging and installing a new program image (ch32rv_flash.h): what Update
 * and the USB firmware classes stand on. Nothing here is linked unless one of
 * them is used; the copy itself costs its size in RAM (.highcode) only then.
 *
 * Why a copy over offset 0 rather than a second slot to boot from: the chips
 * boot the code flash at 0 (or the factory bootloader in the system flash);
 * there is no boot address to switch and no second-stage loader of this
 * core's to pick a slot. The copy is the only step that can leave a broken
 * program, so it runs last, from RAM, with interrupts off, and only after the
 * staged image was written, read back and checked. */
#define CH32RV_FLASH_OP        static inline __attribute__((always_inline))
#define CH32RV_FLASH_OP_INLINE static inline __attribute__((always_inline))
#include "internal/ch32rv_flash_ops.h"
#include "internal/ch32rv_irq.h"

extern uint8_t _ch32rv_flash_length[], _ch32rv_image_end[], _ch32rv_store_start[];

/* Rounded to the largest erase unit of the series, so an area erased by the
 * unit never reaches back into the image (H41x's 960K mode erases 8 KiB). */
uint32_t ch32rv_flash_staging_offset(void)
{
    const uint32_t end = (uint32_t)(uintptr_t)_ch32rv_image_end;
    return (end + CH32RV_FLASH_ERASE_SIZE - 1u) & ~(CH32RV_FLASH_ERASE_SIZE - 1u);
}

uint32_t ch32rv_flash_staging_size(void)
{
    const uint32_t from = ch32rv_flash_staging_offset();
    uint32_t to = (uint32_t)(uintptr_t)_ch32rv_store_start;
    if (to > (uint32_t)(uintptr_t)_ch32rv_flash_length) {
        to = (uint32_t)(uintptr_t)_ch32rv_flash_length;
    }
#if CH32RV_FLASH_SELF_WRITE
    /* The linker's FLASH is the 960K mode's; in 480K mode only that much is there. */
    if (to > ch32rv_flash_size()) {
        to = ch32rv_flash_size();
    }
#endif
    return to > from ? (to - from) & ~(CH32RV_FLASH_ERASE_SIZE - 1u) : 0u;
}

bool ch32rv_flash_image_plausible(uint32_t head, uint32_t length)
{
    if (length < 8u) {
        return false;
    }
    if ((head & 0xFFFu) == 0x06Fu) {                  /* jal x0 (j): the vector table's entry 0 */
        const uint32_t target = ((head >> 21) & 0x3FFu) << 1 | ((head >> 20) & 1u) << 11 |
                                ((head >> 12) & 0xFFu) << 12 | (head >> 31) << 20;
        return !(head >> 31) && target >= 4u && target < length;
    }
    if ((head & 0xE003u) == 0xA001u) {                /* c.j: a compressed jump forward */
        const uint32_t h = head & 0xFFFFu;
        const uint32_t target = ((h >> 3) & 7u) << 1 | ((h >> 11) & 1u) << 4 | ((h >> 2) & 1u) << 5 |
                                ((h >> 7) & 1u) << 6 | ((h >> 6) & 1u) << 7 | ((h >> 9) & 3u) << 8 |
                                ((h >> 8) & 1u) << 10;
        return !((h >> 12) & 1u) && target >= 2u && target < length;
    }
    /* csrci / csrrci (SYSTEM): H41x's V5F image starts by masking interrupts. */
    return (head & 0x707Fu) == 0x7073u;
}

/* The copy: no call leaves it, and no table or constant is read from flash. */
__attribute__((section(".highcode"), noinline, noreturn)) static void install_from_ram(uint32_t from, uint32_t length, uint32_t unit)
{
    uint32_t page[CH32RV_FLASH_WRITE_SIZE / 4u];
    ch32rv_irq_disable_all();   /* gintenr or mstatus by core (ch32rv_irq.h): no interrupt from here on */
    unlock();
    for (uint32_t a = 0; a < length; a += unit) {
        for (uint32_t tries = 0; tries < 3u; tries++) {
#ifdef CH32RV_IWDG_BASE
            CH32RV_IWDG_CTLR = CH32RV_IWDG_KEY_FEED;                    /* a running watchdog would cut the copy */
#endif
            bool ok = erase_one(FLASH_MEM | a);
            for (uint32_t p = a; ok && p < a + unit && p < length; p += CH32RV_FLASH_WRITE_SIZE) {
                const volatile uint32_t *src = (const volatile uint32_t *)(uintptr_t)(FLASH_MEM | (from + p));
                for (uint32_t i = 0; i < CH32RV_FLASH_WRITE_SIZE / 4u; i++) {
                    page[i] = src[i];
                }
                ok = write_one(FLASH_MEM | p, (const uint8_t *)page, true);
                const volatile uint32_t *dst = (const volatile uint32_t *)(uintptr_t)(FLASH_MEM | p);
                for (uint32_t i = 0; ok && i < CH32RV_FLASH_WRITE_SIZE / 4u; i++) {
                    ok = dst[i] == page[i];
                }
            }
            if (ok) {
                break;
            }
        }
    }
    lock();
    for (;;) {
        CH32RV_SYSTEM_RESET();
    }
}

void ch32rv_flash_install(uint32_t from, uint32_t length)
{
    /* The image starts the copy's source on a write unit: what the staging
     * wrote. Lengths round up to whole pages (the padding is erased flash or
     * the stager's fill). */
    length = (length + CH32RV_FLASH_WRITE_SIZE - 1u) & ~(CH32RV_FLASH_WRITE_SIZE - 1u);
    install_from_ram(from, length, ch32rv_flash_erase_size());
}
