/* Erasing and programming the code flash from the running program.
 *
 * Advanced use. For data that must last, an SD card or an I2C EEPROM is the
 * recommended place (docs/flash-store.ja.md). The code flash wears out (10K
 * erases on some series); re-uploading a sketch may erase what was kept
 * (depends on the route and the tool version); it is erased by whole units
 * and programmed by whole pages; and on V003, V00x, M030, V103, L103, X035
 * and V205 the CPU and interrupts stop while it is busy (a page takes about
 * 6 ms on V003: UART bytes are lost and millis() falls behind).
 *
 * Every series has the same functions; each costs nothing unless called.
 * Offsets count from the start of the code flash, which is also the link
 * address (ORIGIN(FLASH) is 0 on every series): CH32RV_FLASH_OFFSET() turns a
 * pointer into a CH32RV_FLASH_STORE area into one, and the area reads as
 * ordinary memory.
 *
 * The functions refuse - return false and touch nothing - a range that is not
 * aligned to its unit, is empty, runs past FLASH or, where the program runs
 * from flash, overlaps the program itself. CH32H41x run from RAM
 * (CH32RV_FLASH_SELF_WRITE), so there the whole flash, the program's own image
 * included, can be rewritten and the next reset runs what was written. They
 * also return false on a write protection error or when the flash does not
 * finish; what the range then holds is undefined.
 *
 * Steps: EVT FLASH_ErasePage_Fast / FLASH_ProgramPage_Fast / FLASH_BufLoad of
 * each series, or FLASH_ROM_ERASE / FLASH_ROM_WRITE where there is no fast
 * erase (V4x7, X3x5, H41x). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
#define CH32RV_FLASH_SELF_WRITE 1
#else
#define CH32RV_FLASH_SELF_WRITE 0
#endif

/* CH32RV_FLASH_ERASE_SIZE: the smallest erase. CH32RV_FLASH_WRITE_SIZE: the
 * page programmed at once. */
#if defined(CH32RV_SERIES_CH32V003)
#define CH32RV_FLASH_ERASE_SIZE 64u
#define CH32RV_FLASH_WRITE_SIZE 64u
#elif defined(CH32RV_SERIES_CH32V103) || defined(CH32RV_SERIES_CH32M030)
#define CH32RV_FLASH_ERASE_SIZE 128u
#define CH32RV_FLASH_WRITE_SIZE 128u
#elif defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467) || defined(CH32RV_SERIES_CH32X315) || \
    defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32H416)
#define CH32RV_FLASH_ERASE_SIZE 4096u
#define CH32RV_FLASH_WRITE_SIZE 256u
#elif defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H415)
/* 960K (FLASH_CFGR0.DBMODE set) erases by 8 KiB; ch32rv_flash_erase_size()
 * reads the mode. */
#define CH32RV_FLASH_ERASE_SIZE 8192u
#define CH32RV_FLASH_WRITE_SIZE 256u
#else
#define CH32RV_FLASH_ERASE_SIZE 256u
#define CH32RV_FLASH_WRITE_SIZE 256u
#endif

/* What an erased word reads as. Tell written data by a version or a check
 * value, not by blank bytes: the value differs by series. */
#if defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V303) ||  \
    defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317) ||  \
    defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467) || defined(CH32RV_SERIES_CH32X315) ||  \
    defined(CH32RV_SERIES_CH32X305) || CH32RV_FLASH_SELF_WRITE
#define CH32RV_FLASH_ERASED_WORD 0xE339E339u
#else
#define CH32RV_FLASH_ERASED_WORD 0xFFFFFFFFu
#endif

/* Reserve `size` bytes (rounded up to whole erase units) for `name`, packed
 * against the end of FLASH and kept out of the uploaded image. Nothing is
 * reserved unless the sketch uses `name`. Read it as memory; change it only
 * through the functions below. */
#define CH32RV_FLASH_STORE(name, size)                                                                  \
    __attribute__((section(".ch32rv_store"), aligned(CH32RV_FLASH_ERASE_SIZE))) uint8_t               \
        name[((size) + CH32RV_FLASH_ERASE_SIZE - 1u) / CH32RV_FLASH_ERASE_SIZE * CH32RV_FLASH_ERASE_SIZE]

#define CH32RV_FLASH_OFFSET(p) ((uint32_t)(uintptr_t)(p))

#ifdef __cplusplus
extern "C" {
#endif

/* The code flash this build uses (FLASH in the linker script; on H41x, what
 * FLASH_CFGR0.DBMODE gives), and its erase unit. */
uint32_t ch32rv_flash_size(void);
uint32_t ch32rv_flash_erase_size(void);

/* Erase [offset, offset + length): multiples of the erase unit. */
bool ch32rv_flash_erase(uint32_t offset, uint32_t length);
/* Program erased flash: offset and length multiples of
 * CH32RV_FLASH_WRITE_SIZE; data any alignment. */
bool ch32rv_flash_write(uint32_t offset, const void *data, uint32_t length);

/* One unit each: cheaper than the range forms when the offset is not a
 * constant. data: CH32RV_FLASH_WRITE_SIZE bytes. */
bool ch32rv_flash_erase_page(uint32_t offset);
bool ch32rv_flash_write_page(uint32_t offset, const uint32_t *data);
/* Erase the unit at offset and program its first CH32RV_FLASH_WRITE_SIZE
 * bytes from data, in one go: cheaper than erase_page then write_page. */
bool ch32rv_flash_rewrite_page(uint32_t offset, const uint32_t *data);

#ifdef __cplusplus
}
#endif
