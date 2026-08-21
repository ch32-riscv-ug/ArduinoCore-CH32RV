/* Entry point. crt0_ch32.S sets the stack, copies .data, zeroes .bss, runs
 * .init_array and jumps here through mret.
 *
 * The three hooks below are Arduino's, not ours, and they exist because
 * sketches and libraries in the wild use them:
 *
 *   initVariant()   a variant or a library may define it, and some do. Called
 *                   once before setup().
 *
 * init() is deliberately not among them. On AVR that is the *core's* own
 * hardware setup - it configures the timers and the ADC - not a user hook, and
 * ours is already done by SystemInit and crt0 before main runs. Calling an
 * empty one would be ceremony that every sketch pays for.
 *   serialEventRun() dispatches serialEvent() after every loop().
 *                   api/HardwareSerial.h declares it weak; the definition sits
 *                   in HardwareSerial.cpp, so it only gets linked into sketches
 *                   that use Serial. Everywhere else the pointer is null and
 *                   the call is skipped.
 */
#include "Arduino.h"

/* Empty by default; a variant or a library that needs something to happen
 * before setup() overrides it. */
extern "C" __attribute__((weak)) void initVariant(void)
{
}

int main(void)
{
    initVariant();

    setup();
    for (;;) {
        loop();
        if (serialEventRun) {
            serialEventRun();
        }
    }
}
