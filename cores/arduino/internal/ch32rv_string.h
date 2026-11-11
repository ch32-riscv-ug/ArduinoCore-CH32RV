/* Shared by ch32rv_string.c, ch32rv_memmove.c and ch32rv_memset.c. The
 * compiler would turn their loops back into calls to these very functions, so
 * they are built without that pattern: GCC by its optimize attribute, clang
 * (which ignores that one) by no_builtin, which keeps its loop-idiom pass from
 * emitting library calls in the function. */
#pragma once

#if defined(__clang__)
#define CH32RV_STRING_FN __attribute__((no_builtin))
#else
#define CH32RV_STRING_FN __attribute__((optimize("no-tree-loop-distribute-patterns")))
#endif
