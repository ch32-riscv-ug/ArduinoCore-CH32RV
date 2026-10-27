/* For CH32RVSPISlave (its own library, so that a sketch using only SPI links
 * none of its interrupt handlers): the route search and the claim of
 * SPI.begin(sck, miso, mosi), without the controller's pin modes and
 * settings. The same owner table, so a bus and a slave never share an SPI
 * (on a board with one SPI there is no table: do not open both). */
#pragma once

#include <stdint.h>

struct ch32rv_spi_claim_t {
    uint32_t base;
    uint32_t clken_addr;
    uint32_t clken_mask;
    uint8_t index;       /* row in SPI.cpp's table */
    uint8_t number;      /* n of SPIn */
    uint8_t sck, miso, mosi;
};

/* False when the three pins are no free SPI's route. On success the SPI's
 * clock is on, the route selected and the pads given its alternate function;
 * their modes (input / output) are the caller's. */
bool ch32rv_spi_claim(const void *who, int sck, int miso, int mosi, ch32rv_spi_claim_t *out);
/* SPI off, clock off, the three pads back to floating inputs. */
void ch32rv_spi_release(const void *who, const ch32rv_spi_claim_t *c);
