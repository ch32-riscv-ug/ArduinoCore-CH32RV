/* UIAPduino Pro Micro CH32V003 V1.4 product-board variant. */
#pragma once

/* The QFN20 package masks in the generated silicon variant are selected by
 * CH32RV_PART_CH32V003F4U6, which boards.txt passes (build.part). */

/* Board facts must precede the L0 header because its standard names are
 * intentionally guarded for product-board overrides. */
#define LED_BUILTIN PC0
#define PIN_LED     PC0

#include "../CH32V003/pins_arduino.h"

/* The silkscreen numbers 0..17, as the released UIAPduino core exposes them.
 * Pad names keep their port-encoded values (PA1 is 65 here as on every CH32
 * board); a number below CH32RV_BOARD_PIN_COUNT is looked up in this list at
 * the core's entry points (ADR-0010, Arduino.h CH32RV_PIN_RESOLVE). So
 * digitalWrite(13, ...), digitalWrite(D13, ...) and digitalWrite(PD3, ...)
 * all reach PD3. D7/D8/D9 occur on both sides but name one MCU pad each. */
#define CH32RV_BOARD_PIN_COUNT 18
#define CH32RV_BOARD_PINS { \
    PA1, PA2, PC0, PC1, PC2, PC3, PC4, PC5, PC6, PC7, \
    PD0, PD1, PD2, PD3, PD4, PD5, PD6, PD7, \
}

/* The printed numbers run 0..17; the pads behind them are above. */
#undef NUM_DIGITAL_PINS
#undef PINS_COUNT
#define NUM_DIGITAL_PINS CH32RV_BOARD_PIN_COUNT
#define PINS_COUNT       NUM_DIGITAL_PINS

#define D0  PA1
#define D1  PA2
#define D2  PC0
#define D3  PC1
#define D4  PC2
#define D5  PC3
#define D6  PC4
#define D7  PC5
#define D8  PC6
#define D9  PC7
#define D10 PD0
#define D11 PD1
#define D12 PD2
#define D13 PD3
#define D14 PD4
#define D15 PD5
#define D16 PD6
#define D17 PD7

/* TX / RX are the series default, PD5 / PD6 - D15 / D16 on this board. */

/* Board-level reserved functions. They remain valid pin constants, but test
 * fixtures use these names to exclude them from generic GPIO sweeps. */
#define PIN_UIAP_SWIO  PD1
#define PIN_UIAP_RESET PD7
#define PIN_USB_DP     PD3
#define PIN_USB_DM     PD4

/* Compatibility with the extension exposed by UIAP's official core. PinName
 * is a physical-port encoding there (PD_1 == 0x31), not the Arduino number
 * PD1/D11. Disabling SWIO is intentionally explicit and remains in effect
 * until reset; ordinary pinMode(PD1, ...) does not do it behind the user's
 * back, so a fixture can always recover the board over SWIO. */
typedef uint8_t PinName;
#define PD_1 ((PinName)0x31u)

static inline void pinV32_DisconnectDebug(PinName pin)
{
#ifndef CH32V_LOCK_DEBUG
    if (pin == PD_1) {
        volatile uint32_t *const apb2pcenr =
            (volatile uint32_t *)(uintptr_t)CH32RV_CLKEN_AFIO_ADDR;
        volatile uint32_t *const pcfr1 =
            (volatile uint32_t *)(uintptr_t)0x40010004u;
        *apb2pcenr |= CH32RV_CLKEN_AFIO_MASK;
        *pcfr1 = (*pcfr1 & ~0x07000000u) | 0x04000000u;
    }
#else
    (void)pin;
#endif
}

static inline void pin_DisconnectDebug(PinName pin)
{
    pinV32_DisconnectDebug(pin);
}
