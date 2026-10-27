/* Pin wakeups (ch32rv_power.h), on their own: they need the EXTI handlers
 * (wiring_interrupts.c), and a timer-only sleep does not. */
#include "Arduino.h"
#include "ch32rv_power.h"

void ch32rv_exti_configure(pin_size_t pin, PinStatus mode);   /* wiring_interrupts.c */

int ch32rv_power_add_pin(int pin, int level)
{
    struct ch32rv_power_wake *w = &ch32rv_power_wake;
    if (!ch32rv_power_supported() || w->pins >= 4 || !digitalPinIsValid(pin)) {
        return 0;
    }
    ch32rv_exti_configure((pin_size_t)pin, level == HIGH ? RISING : FALLING);
    w->pin[w->pins] = (int16_t)pin;
    w->level[w->pins] = level == HIGH ? HIGH : LOW;
    w->pins++;
    return 1;
}
