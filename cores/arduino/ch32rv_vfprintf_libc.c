/* The Floating Point menu's picolibc printf entries (CH32RV_PRINTF_LIBC):
 * picolibc's vfprintf - which its printf, fprintf, snprintf and the rest
 * call - is the integer-only one by default; this makes it the double one
 * (%f %e %g). A file of its own: core.a is searched before libc, so this
 * member is linked only when something calls vfprintf, and a sketch that
 * never formats links neither. */
#include <stdarg.h>
#include <stdio.h>

#if CH32RV_PRINTF_LIBC
int vfprintf(FILE *stream, const char *format, va_list ap)
{
    return __d_vfprintf(stream, format, ap);
}
#endif
