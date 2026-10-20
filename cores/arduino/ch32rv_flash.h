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
