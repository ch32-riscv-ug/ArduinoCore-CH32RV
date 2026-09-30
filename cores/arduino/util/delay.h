/* avr-libc's <util/delay.h>, for libraries that include it unconditionally on
 * any architecture they do not know (Adafruit_SSD1306 does, for one). The
 * busy waits map onto the core's delay() / delayMicroseconds(). */
#pragma once

#include "Arduino.h"

#define _delay_ms(ms) delay((unsigned long)(ms))
#define _delay_us(us) delayMicroseconds((unsigned int)(us))
