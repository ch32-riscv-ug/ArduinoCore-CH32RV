/* wiring_private.h: the header AVR, SAMD, RP2040, STM32 and ESP32 cores all
 * ship, which display and sensor libraries include unconditionally
 * (Adafruit_ILI9341, Adafruit_ST77xx). Those cores keep their internal pin
 * helpers in it; this core's are in internal/, so the header only brings in
 * the Arduino API. */
#ifndef CH32RV_WIRING_PRIVATE_H
#define CH32RV_WIRING_PRIVATE_H

#include "Arduino.h"

#endif
