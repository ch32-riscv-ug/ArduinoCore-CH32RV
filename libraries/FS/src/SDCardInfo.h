/* What the SD (SPI) and SD_MMC (SDIO) libraries share: the card types (ESP32's sdcard_type_t) and the
 * capacity from a CSD register. */
#pragma once

#include <stdint.h>

typedef enum { CARD_NONE, CARD_MMC, CARD_SD, CARD_SDHC, CARD_UNKNOWN } sdcard_type_t;

/* Blocks of 512 bytes from the 16-byte CSD (most significant byte first):
 * CSD 1.0 (SDSC, MMC), 2.0 (SDHC / SDXC). 0 for a structure it does not know. */
static inline uint32_t sd_csd_blocks(const uint8_t csd[16])
{
    const uint8_t structure = csd[0] >> 6;
    if (structure == 1) {
        const uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
        return (c_size + 1u) * 1024u;
    }
    if (structure == 0 || structure == 2 || structure == 3) {   /* SDSC, MMC (2, 3: MMC's own versions) */
        const uint32_t c_size = ((uint32_t)(csd[6] & 0x03) << 10) | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
        const uint8_t mult = (uint8_t)(((csd[9] & 0x03) << 1) | (csd[10] >> 7));
        const uint8_t bl_len = csd[5] & 0x0F;
        if (bl_len < 9 || bl_len > 11) {
            return 0;
        }
        return (c_size + 1u) << (mult + 2u + bl_len - 9u);
    }
    return 0;
}
