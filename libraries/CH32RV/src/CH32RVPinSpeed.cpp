/* CH32RV.setPinSpeed() (CH32RVSystem.h): an output's slew rate. On most
 * families it is the pad's MODE bits (MODEy, the reference manuals: 01 a
 * maximum of 10 MHz - pinMode(OUTPUT)'s -, 10 2 MHz, 11 50 MHz; 30 MHz on
 * V003 / V00x); X305 / X315 have a SPEED register of their own (GPIOx_SPEED
 * at 0x1C, two bits a pad: low, medium, high, very high); X033 / X035 and
 * M030 have no setting (their outputs have one speed). */
#include "CH32RVSystem.h"

#include "Arduino.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_pins.h"
#include "internal/ch32rv_registers.h"

namespace arduino {

bool CH32RVSystem::setPinSpeed(int pin, CH32RVPinSpeed speed)
{
    if (pin < 0 || (unsigned)speed > (unsigned)CH32RV_PIN_SPEED_VERY_HIGH) {
        return false;
    }
    pin = CH32RV_PIN_RESOLVE(pin);
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin);
    const uint8_t bit = (uint8_t)CH32RV_PIN_BIT(pin);
    if (port >= CH32RV_PORT_COUNT || !(CH32RV_PORT_MASK(port) & (1u << bit))) {
        return false;
    }
#if defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315)
    volatile uint32_t &reg = CH32RV_REG32(CH32RV_GPIO_BASE(port) + 0x1Cu);
    const uint32_t shift = (uint32_t)(bit & 15u) * 2u;
    if (bit >= 16u) {
        return false;   /* the register covers pads 0-15 */
    }
    reg = (reg & ~(3u << shift)) | ((uint32_t)speed << shift);
    return true;
#elif defined(CH32RV_SERIES_CH32X033) || defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32M030)
    (void)speed;
    return false;
#else
    volatile uint32_t *reg = bit < 8u ? &CH32RV_GPIO_CFGLR(port) : &CH32RV_GPIO_CFGHR(port);
    const uint32_t shift = (uint32_t)(bit & 7u) * 4u;
    const uint32_t cfg = (*reg >> shift) & 0xFu;
    if ((cfg & 3u) == 0u) {
        return false;   /* an input: no speed */
    }
    static const uint8_t mode[] = {2u, 1u, 3u, 3u};   /* low 2 MHz, medium 10 MHz, high and very high the fastest */
    ch32rv_gpio_write_config(port, bit, (cfg & ~3u) | mode[speed]);
    return true;
#endif
}

}   // namespace arduino
