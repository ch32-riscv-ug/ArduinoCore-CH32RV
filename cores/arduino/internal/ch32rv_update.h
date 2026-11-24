/* Receiving a new program image and replacing the running one: the engine
 * under the Update library (arduino-esp32's Update API) and the USB firmware
 * classes (CH32RVUsbDeviceDfu, CH32RVUsbDeviceMscFirmwareDisk). Costs nothing
 * unless called.
 *
 * Where the image goes: the chips boot the code flash at 0, and there is no
 * second slot to boot from and no loader of this core's to choose one, so
 * the image is staged in the flash after the program (ch32rv_flash.h,
 * ch32rv_flash_staging_offset()) and copied over it at the restart
 * (ch32rv_update_restart(), ch32rv_flash_install()). The largest image is
 * ch32rv_flash_staging_size(): roughly the flash less the running program.
 *
 * Checked before the copy, so a broken image never starts:
 *   - each page as it is programmed reads back as written (WRITE);
 *   - the image starts with this core's reset entry, a jump inside the image
 *     (MAGIC_BYTE: on the first page, and again at the end with the real
 *     length);
 *   - in order (begin with random false): the length is all of begin()'s
 *     size, and the staged flash's CRC-32 is that of the bytes written
 *     (SIZE / VERIFY at end).
 *   - out of order (random true): the writer counts its own pieces (UF2's
 *     block numbers); written pages are not written again.
 * Nothing touches the program until ch32rv_update_restart(); a reset of any
 * other kind forgets the staged image. One update at a time. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The error codes are arduino-esp32's Update's (UPDATE_ERROR_*), plus VERIFY. */
#define CH32RV_UPDATE_OK           0u
#define CH32RV_UPDATE_WRITE        1u   /* a page did not program or read back */
#define CH32RV_UPDATE_ERASE        2u
#define CH32RV_UPDATE_SPACE        4u   /* bigger than the staging area, or past begin()'s size */
#define CH32RV_UPDATE_SIZE         5u   /* size 0, or unknown for an out-of-order update */
#define CH32RV_UPDATE_MAGIC_BYTE   8u   /* not an image of this core */
#define CH32RV_UPDATE_NO_SPACE     10u  /* no staging area at all (the program fills the flash) */
#define CH32RV_UPDATE_BAD_ARGUMENT 11u  /* an out-of-order piece not on page boundaries */
#define CH32RV_UPDATE_ABORT        12u  /* ended short, or abort() */
#define CH32RV_UPDATE_VERIFY       20u  /* the staged image does not read back as written */

#define CH32RV_UPDATE_SIZE_UNKNOWN 0xFFFFFFFFu

#ifdef __cplusplus
extern "C" {
#endif

/* Start an update of `size` bytes (CH32RV_UPDATE_SIZE_UNKNOWN: up to the
 * staging area, in order only). random: the pieces come in any order
 * (ch32rv_update_write_at()), and the whole size is erased now. */
bool ch32rv_update_begin(uint32_t size, bool random);
/* The next `len` bytes, in order. */
bool ch32rv_update_write(const void *data, uint32_t len);
/* `len` bytes at `offset`, both multiples of CH32RV_FLASH_WRITE_SIZE. The
 * same bytes over written ones are accepted (a piece sent twice). */
bool ch32rv_update_write_at(uint32_t offset, const void *data, uint32_t len);
/* Check what was staged; on success it is what ch32rv_update_restart()
 * installs. even_if_remaining: an in-order update may end short of its size. */
bool ch32rv_update_end(bool even_if_remaining);
void ch32rv_update_abort(void);

uint8_t ch32rv_update_error(void);
void ch32rv_update_clear_error(void);
bool ch32rv_update_running(void);
uint32_t ch32rv_update_size(void);
uint32_t ch32rv_update_progress(void);
/* ch32rv_update_end() succeeded and restart would install. */
bool ch32rv_update_pending(void);
void ch32rv_update_cancel_pending(void);
/* The CRC-32 (ch32rv_crc32()) of the staged image, once ended. */
uint32_t ch32rv_update_crc32(void);
/* Install the pending image and reset into it; with none, a plain reset. */
__attribute__((noreturn)) void ch32rv_update_restart(void);

#ifdef __cplusplus
}
#endif
