/* dtostrf: upstream ships the implementation as a .c.impl for cores to pull in
 * (samd and renesas do the same), so the core only has to place it. */
#include "api/deprecated-avr-comp/avr/dtostrf.h"
#include "api/deprecated-avr-comp/avr/dtostrf.c.impl"
