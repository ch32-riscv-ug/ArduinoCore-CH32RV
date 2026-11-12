/* stdin, stdout and stderr: picolibc leaves its standard streams to the
 * program. Alone in their file so that only a sketch that uses a FILE
 * (fputs(), fprintf(), fwrite() on stdout, or libc++'s abort message on
 * stderr) links the monitor port's writer behind them; printf(), puts() and
 * putchar() are the core's and write without a FILE (ch32rv_stdio_out.c).
 * stdout and stderr write each character to the monitor port as it comes,
 * unbuffered; stdin is at end of file. */
#include <stdio.h>

#include "internal/ch32rv_serial_write.h"

static int ch32rv_stdio_put(char c, FILE *f)
{
    (void)f;
    ch32rv_serial_write_bytes((const uint8_t *)&c, 1);
    return (unsigned char)c;
}

static int ch32rv_stdio_get(FILE *f)
{
    (void)f;
    return _FDEV_EOF;
}

static FILE ch32rv_stdio = FDEV_SETUP_STREAM(ch32rv_stdio_put, ch32rv_stdio_get, NULL, _FDEV_SETUP_RW);

FILE *const stdin = &ch32rv_stdio;
FILE *const stdout = &ch32rv_stdio;
FILE *const stderr = &ch32rv_stdio;
