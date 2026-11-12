/* The Floating Point menu's picolibc scanf entries (CH32RV_SCANF_LIBC):
 * picolibc's vfscanf - which its scanf, sscanf and fscanf call - is the
 * integer-only one by default; this makes it the double one (%f %e %g into
 * float and double). Alone in its file for the reason ch32rv_vfprintf_libc.c
 * gives. */
#include <stdarg.h>
#include <stdio.h>

#if CH32RV_SCANF_LIBC
int vfscanf(FILE *stream, const char *format, va_list ap)
{
    return __d_vfscanf(stream, format, ap);
}
#endif
