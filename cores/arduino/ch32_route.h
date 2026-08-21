/* Alternate-function routes for one peripheral instance.
 *
 * A CH32 peripheral does not have per-pin alternate functions the way an
 * STM32F4 or an ESP32 does: one AFIO field moves the whole peripheral to one
 * of a handful of numbered pin sets. TX cannot move without RX, and SCL cannot
 * move without SDA. So the unit a sketch selects is the route, and picking
 * pins is really "find the route whose pins these are".
 *
 * The variant generates one table per instance (CH32_SERIAL1_ROUTES and
 * friends) from ch32-device-data. The controlling field's mask is not in the
 * table because it is the same for every route of an instance - only the value
 * differs - and the instance already carries the mask.
 *
 * Routes whose pads differ between part numbers in the same series are left
 * out by the generator: one header serves the whole series, so a route that
 * lands on different pins depending on the package cannot be named here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Roles are positional, in the order the peripheral's own header documents:
 * TX,RX for a USART; SCL,SDA for I2C; SCK,MISO,MOSI for SPI. Unused entries
 * are CH32_ROUTE_NO_PIN. */
#define CH32_ROUTE_PINS 3
#define CH32_ROUTE_NO_PIN 0xFFu

typedef struct {
    /* The route number the datasheet uses: 0 is the reset default, 1 is the
     * first remap. Not the array index - a series can skip a number. */
    uint8_t route;
    uint8_t pins[CH32_ROUTE_PINS];
    uint32_t value;    /* bits to write into PCFR1, within the instance mask */
    uint32_t value2;   /* same for PCFR2, where the field spans it */
} ch32_route_t;

/* Index of the route with this number, or -1. */
static inline int ch32_route_find(const ch32_route_t *table, uint8_t count,
                                  uint8_t route)
{
    for (uint8_t i = 0; i < count; i++) {
        if (table[i].route == route) {
            return (int)i;
        }
    }
    return -1;
}

/* Index of the route that puts these pins in these roles, or -1.
 *
 * All of them have to match the same route, which is the whole point: naming a
 * TX from one route and an RX from another is the mistake this catches, and it
 * is one that hardware cannot do. A CH32_ROUTE_NO_PIN in `want` means "do not
 * care", so a caller can select on a subset of the roles.
 */
static inline int ch32_route_match(const ch32_route_t *table, uint8_t count,
                                   const uint8_t *want)
{
    for (uint8_t i = 0; i < count; i++) {
        bool ok = true;
        for (uint8_t r = 0; r < CH32_ROUTE_PINS; r++) {
            if (want[r] != CH32_ROUTE_NO_PIN && table[i].pins[r] != want[r]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            return (int)i;
        }
    }
    return -1;
}
