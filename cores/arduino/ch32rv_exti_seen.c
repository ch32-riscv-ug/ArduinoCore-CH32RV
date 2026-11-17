/* Every EXTI line that has fired, for the low-power code: a pin wakeup or an
 * attachExtiLine() line ends a sleep, and the sleep loop asks whether it came
 * (ch32rv_power.c, weakly). Its own file: wiring_interrupts.c (pins) and
 * ch32rv_exti_line.c (the other lines) each bring it, neither the other. */
#include <stdint.h>

volatile uint32_t ch32rv_exti_seen;
