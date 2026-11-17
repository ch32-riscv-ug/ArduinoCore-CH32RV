/* The EXTI lines that are not pins as wake sources (ch32rv_power.h), on their
 * own: they bring ch32rv_exti_line.c, and a sleep without them does not. */
#include "Arduino.h"
#include "internal/ch32rv_power.h"
#include "internal/ch32rv_exti_line.h"

int ch32rv_power_add_line(uint8_t line, int mode)
{
#ifdef CH32RV_EXTI_LINE_MASK
    /* Attached again with the callback it has (or none), with this edge. */
    if (!ch32rv_power_supported() ||
        !ch32rv_exti_line_attach(line, ch32rv_exti_line_callback(line), mode)) {
        return 0;
    }
    ch32rv_power_wake.lines |= 1u << line;
    return 1;
#else
    (void)line;
    (void)mode;
    return 0;
#endif
}
