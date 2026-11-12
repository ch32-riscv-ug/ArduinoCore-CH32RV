/* printf, vprintf, puts and putchar straight to the stdout port
 * (ch32rv_stdout.cpp) on the core's formatter, in place of picolibc's, which
 * go through a FILE and picolibc's formatter. The compiler may turn
 * printf("text\n") into puts() and printf("c") into putchar(), so all four
 * are here and keep their order. fputs(), fwrite() and fprintf() on stdout are
 * picolibc's, through the unbuffered FILE of ch32rv_stdio_file.c, so they keep
 * the order too. The picolibc entries of the Floating Point menu keep
 * picolibc's. */
#include "internal/ch32rv_format.h"

#if !CH32RV_PRINTF_LIBC

#include <stdio.h>
#include <string.h>

#include "internal/ch32rv_serial_write.h"

static void to_stdout(void *ctx, const char *s, size_t n)
{
    (void)ctx;
    ch32rv_serial_write_bytes((const uint8_t *)s, n);
}

int vprintf(const char *format, va_list ap)
{
    return (int)ch32rv_vformat(to_stdout, 0, format, ap);
}

int printf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    const int n = vprintf(format, ap);
    va_end(ap);
    return n;
}

int puts(const char *s)
{
    const size_t n = strlen(s);
    to_stdout(0, s, n);
    to_stdout(0, "\n", 1);
    return (int)n + 1;
}

#undef putchar /* a macro over fputc in some C libraries (picolibc) */
int putchar(int c)
{
    const char ch = (char)c;
    to_stdout(0, &ch, 1);
    return (unsigned char)c;
}

#endif
