/* Which C library the core is built against, for the few places where the
 * libraries name or declare the same thing differently (the time zone's public
 * variables, the system calls malloc and time() end in). Everything else in
 * the core is written to the C standard and needs no branch.
 *   CH32RV_LIBC_NEWLIB    newlib / newlib-nano (the GCC toolchain's; today's)
 *   CH32RV_LIBC_PICOLIBC  picolibc
 * Neither: a library the core has not been tried with; only the standard
 * names are defined for it. */
#pragma once

#if defined(__has_include)
#if __has_include(<picolibc.h>)
#define CH32RV_LIBC_PICOLIBC 1
#elif __has_include(<_newlib_version.h>)
#define CH32RV_LIBC_NEWLIB 1
#endif
#endif
