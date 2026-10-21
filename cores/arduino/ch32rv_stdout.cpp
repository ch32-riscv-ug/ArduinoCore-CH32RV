/* printf() bridge, on its own: syscalls.c reaches this file only when stdio is
 * used, and the monitor port reaches it only through a weak reference (below). */
#include "HardwareSerial.h"

using namespace arduino;

#include "ch32rv_serial_write.h"

/* Where printf()/puts() go. A pointer rather than a compile-time choice,
 * because the alternatives to the UART - SDI print, and USB CDC - live in
 * libraries, and the core must not have to know about them to let a sketch
 * pick one.
 *
 * The default, the board's monitor port (Serial), is not named here: naming it
 * linked Serial - its object, vtable and the USART code, about 1.7 KB - into
 * every sketch that pulled in any of stdio, even through a library that never
 * prints (newlib's malloc_stats() beside mallinfo(), sprintf() behind String's
 * float formatting). Instead the monitor port offers itself when it is begun
 * (ch32rv_stdout_offer(), from Serial.begin()), and only if the sketch has not
 * chosen: ch32rv_set_stdout() wins, including ch32rv_set_stdout(nullptr) to
 * discard. printf() before Serial.begin() stays a silent no-op, as before. */
static Print *ch32rv_stdout = nullptr;
static bool ch32rv_stdout_chosen = false;

void ch32rv_set_stdout(Print *out)
{
    ch32rv_stdout = out;
    ch32rv_stdout_chosen = true;
}

Print *ch32rv_get_stdout(void)
{
    return ch32rv_stdout;
}

void ch32rv_stdout_offer(Print *port)
{
    if (!ch32rv_stdout_chosen) {
        ch32rv_stdout = port;
    }
}

extern "C" size_t ch32rv_serial_write_bytes(const uint8_t *data, size_t len)
{
    return ch32rv_stdout ? ch32rv_stdout->write(data, len) : 0;
}
