/* UIAPduino Pro Micro CH32V003 V1.4 product-board variant. */
#pragma once

/* Select the QFN20 package masks in the generated silicon variant. */
#ifndef ARDUINO_CH32V003F4U6
#define ARDUINO_CH32V003F4U6 1
#endif

/* Board facts must precede the L0 header because its standard names are
 * intentionally guarded for product-board overrides. */
#define LED_BUILTIN PC0
#define PIN_LED     PC0

#include "../CH32V003/pins_arduino.h"

/* The generic CH32 variants expose port-encoded pin numbers. The released
 * UIAPduino core instead exposes the Pro Micro silkscreen as the ordinary
 * Arduino numbers 0..17. Preserve that source-level contract on this named
 * product board, while CH32_PIN_PORT/BIT translate to the core's internal
 * encoding. D7/D8/D9 occur on both sides but name one MCU pad each. */
#undef PA1
#undef PA2
#undef PC0
#undef PC1
#undef PC2
#undef PC3
#undef PC4
#undef PC5
#undef PC6
#undef PC7
#undef PD0
#undef PD1
#undef PD2
#undef PD3
#undef PD4
#undef PD5
#undef PD6
#undef PD7

#define PA1  0
#define PA2  1
#define PC0  2
#define PC1  3
#define PC2  4
#define PC3  5
#define PC4  6
#define PC5  7
#define PC6  8
#define PC7  9
#define PD0 10
#define PD1 11
#define PD2 12
#define PD3 13
#define PD4 14
#define PD5 15
#define PD6 16
#define PD7 17

#define CH32_UIAP_ENCODE_PIN(pin) ( \
    (pin) ==  0 ? CH32_PIN(0, 1) : (pin) ==  1 ? CH32_PIN(0, 2) : \
    (pin) ==  2 ? CH32_PIN(2, 0) : (pin) ==  3 ? CH32_PIN(2, 1) : \
    (pin) ==  4 ? CH32_PIN(2, 2) : (pin) ==  5 ? CH32_PIN(2, 3) : \
    (pin) ==  6 ? CH32_PIN(2, 4) : (pin) ==  7 ? CH32_PIN(2, 5) : \
    (pin) ==  8 ? CH32_PIN(2, 6) : (pin) ==  9 ? CH32_PIN(2, 7) : \
    (pin) == 10 ? CH32_PIN(3, 0) : (pin) == 11 ? CH32_PIN(3, 1) : \
    (pin) == 12 ? CH32_PIN(3, 2) : (pin) == 13 ? CH32_PIN(3, 3) : \
    (pin) == 14 ? CH32_PIN(3, 4) : (pin) == 15 ? CH32_PIN(3, 5) : \
    (pin) == 16 ? CH32_PIN(3, 6) : (pin) == 17 ? CH32_PIN(3, 7) : (pin))

#undef CH32_PIN_PORT
#undef CH32_PIN_BIT
#define CH32_PIN_PORT(pin) \
    (CH32_UIAP_ENCODE_PIN(pin) >> CH32_PIN_PORT_BITS)
#define CH32_PIN_BIT(pin) \
    (CH32_UIAP_ENCODE_PIN(pin) & ((1 << CH32_PIN_PORT_BITS) - 1))

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

#define TX  D15
#define RX  D16

/* Board-level reserved functions. They remain valid pin constants, but test
 * fixtures use these names to exclude them from generic GPIO sweeps. */
#define PIN_UIAP_SWIO  PD1
#define PIN_UIAP_RESET PD7
#define PIN_USB_DP     PD3
#define PIN_USB_DM     PD4
