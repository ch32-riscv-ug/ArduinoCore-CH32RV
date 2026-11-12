/* Shared by ch32rv_string.c, ch32rv_memmove.c and ch32rv_memset.c. The
 * compiler would turn their loops back into calls to these very functions, so
 * they are built with no_builtin, which keeps clang's loop-idiom pass from
 * emitting library calls in the function. */
#pragma once

#define CH32RV_STRING_FN __attribute__((no_builtin))
