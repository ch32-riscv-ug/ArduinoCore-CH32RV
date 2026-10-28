/* Writing the code flash from the running program.
 *
 * Only where the program runs from RAM (CH32RV_LOADCODE: CH32H41x), so that
 * erasing and programming cannot pull the code from under the CPU: there the
 * whole flash, the program's own image included, can be rewritten, and the
 * next reset runs what was written. Elsewhere CH32RV_FLASH_SELF_WRITE is 0 and
 * these are not declared.
 *
 * Offsets count from the start of the code flash (the image at offset 0 is
 * what boots). The steps are the EVT's FLASH_ROM_ERASE / FLASH_ROM_WRITE
 * (CH32H417 ch32h417_flash.c): page erase (PER) by 8 KiB when FLASH_CFGR0.
 * DBMODE (bit 28) is set, else by 4 KiB; fast page programming by 256 bytes. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The series built with CH32RV_LOADCODE (generate.py LOADCODE_RAM); that
 * define reaches only the startup assembly, so the series name stands in. */
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
#define CH32RV_FLASH_SELF_WRITE 1
#else
#define CH32RV_FLASH_SELF_WRITE 0
#endif

#if CH32RV_FLASH_SELF_WRITE

#ifdef __cplusplus
extern "C" {
#endif

/* The code flash this part has, and its erase unit. */
uint32_t ch32rv_flash_size(void);
uint32_t ch32rv_flash_erase_size(void);
#define CH32RV_FLASH_WRITE_SIZE 256u

/* Erase [offset, offset + length): both multiples of the erase unit. */
bool ch32rv_flash_erase(uint32_t offset, uint32_t length);
/* Program erased flash: offset and length multiples of
 * CH32RV_FLASH_WRITE_SIZE; data any alignment. */
bool ch32rv_flash_write(uint32_t offset, const void *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif

/* Pages kept across resets, packed against the end of the flash.
 *
 * Advanced use: prefer an SD card or an I2C EEPROM. The flash wears out (10K
 * erases on some series), re-uploading the sketch may erase these pages
 * (depends on the route and the tool version), a page is erased and written
 * whole, and on V003, V103, X035 and L103 the CPU and interrupts stop while it
 * is written (about 6 ms on V003: UART bytes are lost, millis() falls behind).
 * The value of an erased page differs by series (0xFF, or 0xE339 on V20x and
 * V30x), so tell a written page by a version field, not by blank bytes.
 *
 * Series whose smallest erase is 4 KiB (V407, X315, H41x) do not have these. */
#if defined(CH32RV_SERIES_CH32V003)
#define CH32RV_FLASH_PAGE_SIZE 64u
#elif defined(CH32RV_SERIES_CH32V103) || defined(CH32RV_SERIES_CH32M030)
#define CH32RV_FLASH_PAGE_SIZE 128u
#elif defined(CH32RV_SERIES_CH32V002) || defined(CH32RV_SERIES_CH32V004) || defined(CH32RV_SERIES_CH32V005) || \
    defined(CH32RV_SERIES_CH32V006) || defined(CH32RV_SERIES_CH32V007) || defined(CH32RV_SERIES_CH32M007) ||   \
    defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103) || defined(CH32RV_SERIES_CH32X035) ||   \
    defined(CH32RV_SERIES_CH32X033) || defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32V203) ||   \
    defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V303) || defined(CH32RV_SERIES_CH32V305) ||   \
    defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317)
#define CH32RV_FLASH_PAGE_SIZE 256u
#endif

#ifdef CH32RV_FLASH_PAGE_SIZE

/* Reserve `pages` pages for `name` at the end of the flash. Nothing is
 * reserved unless the sketch uses `name`. Read it as memory; write it only with
 * ch32rv_flash_write_page(). */
#define CH32RV_FLASH_STORE(name, pages)                                                    \
    __attribute__((section(".ch32rv_store"), aligned(CH32RV_FLASH_PAGE_SIZE))) uint8_t name[(pages) * \
                                                                                            CH32RV_FLASH_PAGE_SIZE]

#ifdef __cplusplus
extern "C" {
#endif

/* Erase the page at `page` (page aligned, inside a CH32RV_FLASH_STORE area)
 * and program it from `data` (CH32RV_FLASH_PAGE_SIZE bytes). False when the
 * page is outside the reserved area, the flash reported a write protection
 * error or did not finish; the page may then hold anything. */
bool ch32rv_flash_write_page(const void *page, const uint32_t *data);

#ifdef __cplusplus
}
#endif

#endif
