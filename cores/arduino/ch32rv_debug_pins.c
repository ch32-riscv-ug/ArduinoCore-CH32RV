/* The debug interface's pads as ordinary pads (internal/ch32rv_debug_pins.h). */
#include "pins_arduino.h"
#include "internal/ch32rv_debug_pins.h"
#include "internal/ch32rv_registers.h"

/* AFIO_PCFR1.SW_CFG / SWJ_CFG, bits 26:24, on every series: 100b turns the
 * interface off (evidence/register_fields.csv; X035RM: "always used as a SWD
 * port after system reset"). */
#define SW_CFG_MASK 0x07000000u
#define SW_CFG_OFF  0x04000000u

static bool released;

void ch32rv_release_debug_pins(void)
{
#ifdef CH32RV_DEBUG_PAD_COUNT
    CH32RV_REG32(CH32RV_CLKEN_AFIO_ADDR) |= CH32RV_CLKEN_AFIO_MASK;
    CH32RV_AFIO_PCFR1 = (CH32RV_AFIO_PCFR1 & ~SW_CFG_MASK) | SW_CFG_OFF;
    released = true;
#endif
}

bool ch32rv_debug_pins_released(void)
{
    return released;
}

bool ch32rv_debug_pin_locked(uint8_t pin)
{
#ifdef CH32RV_DEBUG_PAD_COUNT
    if (released) {
        return false;
    }
    static const uint8_t pads[CH32RV_DEBUG_PAD_COUNT] = CH32RV_DEBUG_PADS;
    for (uint8_t i = 0; i < CH32RV_DEBUG_PAD_COUNT; i++) {
        if (pads[i] == pin) {
            return true;
        }
    }
#else
    (void)pin;
#endif
    return false;
}
