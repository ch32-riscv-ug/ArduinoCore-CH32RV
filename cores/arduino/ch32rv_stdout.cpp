/* printf() bridge, on its own: the pointer below names the monitor port, and
 * keeping it next to the port code made every sketch that used any port link the
 * monitor port too. syscalls.c reaches this file only when stdio is used. */
#include "HardwareSerial.h"

using namespace arduino;

#include "ch32rv_serial_write.h"

/* Where printf()/puts() go. A pointer rather than a compile-time choice,
 * because the alternatives to the UART - SDI print, and USB CDC - live in
 * libraries, and the core must not have to know about them to let a sketch
 * pick one. The default is the board's monitor port, resolved at link time, so
 * a sketch that never calls ch32rv_set_stdout() behaves exactly as before.
 *
 * A port that has not been begun accepts nothing: write() returns 0 when it is
 * closed, so printf() before Serial.begin() stays a silent no-op rather than a
 * hang. */
static Print *ch32rv_stdout =
#ifdef SERIAL_PORT_MONITOR
    &SERIAL_PORT_MONITOR;
#else
    nullptr;
#endif

void ch32rv_set_stdout(Print *out)
{
    ch32rv_stdout = out;
}

Print *ch32rv_get_stdout(void)
{
    return ch32rv_stdout;
}

extern "C" size_t ch32rv_serial_write_bytes(const uint8_t *data, size_t len)
{
    return ch32rv_stdout ? ch32rv_stdout->write(data, len) : 0;
}
