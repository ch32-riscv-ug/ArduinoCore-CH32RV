/* CH32RV.enableExtiLineWakeup(): apart from the rest of sleep, since it brings
 * the EXTI line code (ch32rv_exti_line.c) and a timer-only sleep needs none. */
#include "CH32RVSystem.h"

#include "Arduino.h"
#include "internal/ch32rv_power.h"

namespace arduino {

bool CH32RVSystem::enableExtiLineWakeup(uint8_t line, PinStatus mode)
{
    return ch32rv_power_add_line(line, mode) != 0;
}

}  // namespace arduino
