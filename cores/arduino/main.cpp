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
 *
 * ch32rv_services_run() is ours, the same weak-pointer shape: the round over
 * the system services the libraries attached in their begin() (USB device and
 * host, the IP stack, USB PD - ch32rv_service.h). It runs after every loop()
 * and inside delay(). It is not hung on yield(), because a sketch may define
 * its own yield().
 *
 * deliverEvents() (ch32rv_deliver.h) runs after every loop() too, so the
 * libraries' notification callbacks arrive without the sketch calling it; a
 * sketch may still call it where it wants them sooner. A weak reference:
 * ch32rv_deliver.o is linked only when a driver attaches a delivery.
 */
#include "Arduino.h"
#include "internal/ch32rv_serial_write.h"
#include "internal/ch32rv_service.h"

#include <string.h>

extern "C" size_t deliverEvents(void) __attribute__((weak));

/* assert()'s end, here because this object is always linked: defined before
 * libc is searched, newlib's assert.o - which prints through fiprintf to
 * stderr - is never pulled in. It was, by String alone: String's toDouble()
 * names atof, whose strtod reaches an assert, and fiprintf brought stdio's
 * three FILEs (__sf), 312 B of RAM on V003 that no code used after LTO had
 * dropped toDouble(). The same words as newlib's, to the same place
 * (_write's), then a stop. Linked only where an assert can fire. */
static void ch32rv_assert_out(const char *text)
{
    if (text) {
        ch32rv_serial_write_bytes((const uint8_t *)text, strlen(text));
    }
}

extern "C" __attribute__((noreturn)) void __assert_func(const char *file, int line,
                                                        const char *func, const char *expr)
{
    char digits[12];
    char *d = digits + sizeof digits;
    unsigned n = line < 0 ? 0u : (unsigned)line;
    *--d = 0;
    do {
        *--d = (char)('0' + n % 10u);
        n /= 10u;
    } while (n);
    ch32rv_assert_out("assertion \"");
    ch32rv_assert_out(expr);
    ch32rv_assert_out("\" failed: file \"");
    ch32rv_assert_out(file);
    ch32rv_assert_out("\", line ");
    ch32rv_assert_out(d);
    if (func) {
        ch32rv_assert_out(", function: ");
        ch32rv_assert_out(func);
    }
    ch32rv_assert_out("\n");
    for (;;) {
    }
}

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
        if (ch32rv_services_run) {
            ch32rv_services_run();
        }
        if (deliverEvents) {
            deliverEvents();
        }
        if (serialEventRun) {
            serialEventRun();
        }
    }
}
