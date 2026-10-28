/* EXPERIMENT (PERSIST-01 M19): the code flash API on every series, with switches for the cost matrix.
 *   FX_RMW      1: read-modify-write CTLR (EVT style; keeps V407's SCKMOD), 0: plain stores
 *   FX_CHECK    1: refuse ranges that are unaligned, outside FLASH, or (XIP series) over the image
 *   FX_ALIGNED  1: ch32rv_flash_write() takes word-aligned data only, 0: any alignment
 * Offsets count from the start of the code flash (= the link address: ORIGIN(FLASH) is 0 on every series). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
#define CH32RV_FLASH_SELF_WRITE 1
#else
#define CH32RV_FLASH_SELF_WRITE 0
#endif

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
#define CH32RV_FLASH_ERASE_SIZE 8192u /* DBMODE (960K); the 480K mode erases by 4 KiB */
#define CH32RV_FLASH_WRITE_SIZE 256u
#else
#define CH32RV_FLASH_ERASE_SIZE 256u
#define CH32RV_FLASH_WRITE_SIZE 256u
#endif

#if defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V303) ||       \
    defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317) ||       \
    defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467) || defined(CH32RV_SERIES_CH32X315) ||       \
    defined(CH32RV_SERIES_CH32X305) || CH32RV_FLASH_SELF_WRITE
#define CH32RV_FLASH_ERASED_WORD 0xE339E339u
#else
#define CH32RV_FLASH_ERASED_WORD 0xFFFFFFFFu
#endif

#define CH32RV_FLASH_STORE(name, size)                                                                        \
    __attribute__((section(".ch32rv_store"), aligned(CH32RV_FLASH_ERASE_SIZE))) uint8_t                     \
        name[((size) + CH32RV_FLASH_ERASE_SIZE - 1u) / CH32RV_FLASH_ERASE_SIZE * CH32RV_FLASH_ERASE_SIZE]

#ifdef __cplusplus
extern "C" {
#endif

uint32_t ch32rv_flash_size(void);
uint32_t ch32rv_flash_erase_size(void);

bool ch32rv_flash_erase(uint32_t offset, uint32_t length);
bool ch32rv_flash_write(uint32_t offset, const void *data, uint32_t length);
bool ch32rv_flash_erase_page(uint32_t offset);
bool ch32rv_flash_write_page(uint32_t offset, const uint32_t *data);
bool ch32rv_flash_rewrite_page(uint32_t offset, const uint32_t *data);

#ifdef __cplusplus
}
#endif
