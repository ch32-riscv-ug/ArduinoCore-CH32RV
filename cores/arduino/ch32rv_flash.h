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
/* 960K (FLASH_CFGR0.DBMODE set) erases by 8 KiB, 480K by 4 KiB: the unit
 * is 8 KiB in both (two sectors in 480K). */
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

/* Replacing the program (Update, the USB firmware classes): a new image is
 * staged in the flash after this one, checked, then copied over offset 0 and
 * the chip reset. The staging area runs from the first erase unit after the
 * image to the CH32RV_FLASH_STORE areas, so they survive an update; an image
 * as long as the area fits, since the copy runs forward and the bytes it
 * overwrites in the area have been copied already.
 *
 * ch32rv_flash_install() copies `length` bytes from `from` (an offset in the
 * staging area) to offset 0 with interrupts off, from RAM (CH32RV_RAMFUNC:
 * the RAM it takes is linked only with it), feeding the independent watchdog
 * as it goes, retrying a unit that does not read back, and resets. It does
 * not return. Losing power during the copy leaves a part-written program:
 * the factory bootloader (BOOT0 / BOOT_MODE) and the debug interface are
 * outside the code flash and still reach the chip. */
uint32_t ch32rv_flash_staging_offset(void);
uint32_t ch32rv_flash_staging_size(void);
__attribute__((noreturn)) void ch32rv_flash_install(uint32_t from, uint32_t length);

/* Whether `head` (the image's first 4 bytes, little-endian) starts like an
 * image this core links: its reset entry, a jump (32- or 16-bit) to inside
 * the `length` bytes, or the CSR access that starts H41x's V5F image. A cheap
 * refusal of what is not a program at all - a text file, an ELF, a UF2 or
 * hex file sent as raw bytes; it does not tell one series' image from
 * another's. */
bool ch32rv_flash_image_plausible(uint32_t head, uint32_t length);

#ifdef __cplusplus
}
#endif

/* The UF2 family ID of this series (github.com/microsoft/uf2,
 * utils/uf2families.json): the registered ones where WCH has one -
 * CH32V2xx / CH32V3xx 0x699B62EC, CH32V407 / V467 0x7B8C32A7 - and, for the
 * series the registry does not list, IDs this core chose (docs/usb.ja.md,
 * "UF2"). One registered ID covers V203 to V317: it keeps a V4x7 or X035
 * image out, not a V203 image off a V307. */
#if defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V205) || \
    defined(CH32RV_SERIES_CH32V303) || defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || \
    defined(CH32RV_SERIES_CH32V317)
#define CH32RV_UF2_FAMILY_ID 0x699B62ECu
#elif defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
#define CH32RV_UF2_FAMILY_ID 0x7B8C32A7u
#elif defined(CH32RV_SERIES_CH32V103)
#define CH32RV_UF2_FAMILY_ID 0x5C1A4E6Bu
#elif defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103)
#define CH32RV_UF2_FAMILY_ID 0x2E97D0C3u
#elif defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
#define CH32RV_UF2_FAMILY_ID 0x8F3B26A1u
#elif defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315)
#define CH32RV_UF2_FAMILY_ID 0x46D8E15Fu
#elif defined(CH32RV_SERIES_CH32M030)
#define CH32RV_UF2_FAMILY_ID 0xB2709C34u
#elif defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
#define CH32RV_UF2_FAMILY_ID 0xD5E3A87Au
#elif defined(CH32RV_SERIES_CH32V003) || defined(CH32RV_SERIES_CH32V002) || defined(CH32RV_SERIES_CH32V004) || \
    defined(CH32RV_SERIES_CH32V005) || defined(CH32RV_SERIES_CH32V006) || defined(CH32RV_SERIES_CH32V007) || \
    defined(CH32RV_SERIES_CH32M007)
#define CH32RV_UF2_FAMILY_ID 0x1F64C9E2u
#endif
