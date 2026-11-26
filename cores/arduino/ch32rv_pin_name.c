/* digitalPinToName() (Arduino.h): a pin's pad name for printing. */
#include "Arduino.h"

const char *digitalPinToName(pin_size_t pin)
{
    static char name[5];   /* "PC19" and a terminator: ports A..F, bits 0..23 */
    const uint32_t pad = CH32RV_PIN_RESOLVE(pin);
    if (!ch32rv_pad_is_valid(pad)) {
        return "NC";
    }
    const unsigned bit = CH32RV_PIN_BIT(pad);
    char *p = name;
    *p++ = 'P';
    *p++ = (char)('A' + CH32RV_PIN_PORT(pad));
    if (bit >= 10u) {
        *p++ = (char)('0' + bit / 10u);
    }
    *p++ = (char)('0' + bit % 10u);
    *p = '\0';
    return name;
}
