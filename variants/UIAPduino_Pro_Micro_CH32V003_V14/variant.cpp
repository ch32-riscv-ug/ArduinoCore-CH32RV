/* UIAPduino Pro Micro CH32V003 V1.4: board-level startup.
 *
 * The board keeps a fixed pull-up on USB D- (R10 through LED2), so as soon as
 * the CH32V003 runs a sketch without a software USB stack the host sees a
 * device that never answers a descriptor request ("unknown USB device
 * 0000:0002" on Windows). Holding PD4 (D-) low presents a disconnect (SE0)
 * instead, and the host lists nothing (measured 2026-09-22 with the OEP jig:
 * PD4 low -> no device, PD4 released -> 0000:0002, twice).
 *
 * A sketch or library that runs the software USB stack configures PD3/PD4
 * itself when it starts, so this default costs it nothing; it only changes
 * what a plain sketch looks like from the PC. */
#include "Arduino.h"

extern "C" void initVariant(void)
{
    pinMode(PD4, OUTPUT);
    digitalWrite(PD4, LOW);
}
