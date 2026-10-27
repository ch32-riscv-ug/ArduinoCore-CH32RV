/* CH32RV.enablePinWakeup(): apart from the rest of sleep, since it brings the
 * EXTI handlers in (wiring_interrupts.c) and a timer-only sleep needs none. */
#include "CH32RVSystem.h"

#include "Arduino.h"
#include "ch32rv_power.h"

namespace arduino {

bool CH32RVSystem::enablePinWakeup(int pin, int level)
{
    return ch32rv_power_add_pin(pin, level);
}

}  // namespace arduino
