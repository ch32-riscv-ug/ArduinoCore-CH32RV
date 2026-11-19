/* The USB PD CC pads' Rd, off where the sketch takes the pad (internal/ch32rv_gpio.h). */
#include <Arduino.h>

#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_pins.h"
#include "internal/ch32rv_registers.h"

#if defined(CH32RV_USBPD_CC_PADS)

typedef struct {
    uint8_t pin;
    uint32_t port_cc;       /* PORT_CCx */
    uint8_t width;          /* its bits (M030's are 8) */
    uint32_t clock, mask;   /* the USBPD block's clock enable */
} CcPad;

static const CcPad cc_pads[] = CH32RV_USBPD_CC_PADS;

#define CCX_PD 0x02u        /* PORT_CCx bit 1: Rd on (its reset value) */

void ch32rv_usbpd_cc_release(uint8_t port, uint8_t bit)
{
    const uint8_t pin = (uint8_t)CH32RV_PIN(port, bit);
    for (unsigned i = 0; i < sizeof cc_pads / sizeof cc_pads[0]; i++) {
        const CcPad *c = &cc_pads[i];
        if (c->pin != pin) {
            continue;
        }
        /* PORT_CCx takes no write with the block's clock off: on for the write, then as it was. */
        const uint32_t was = CH32RV_REG32(c->clock) & c->mask;
        CH32RV_REG32(c->clock) |= c->mask;
        if (c->width == 8u) {
            CH32RV_REG8(c->port_cc) &= (uint8_t)~CCX_PD;
        } else {
            CH32RV_REG16(c->port_cc) &= (uint16_t)~CCX_PD;
        }
        if (!was) {
            CH32RV_REG32(c->clock) &= ~c->mask;
        }
        return;
    }
}

#endif
