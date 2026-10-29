/* Shared by ch32rv_string.c, ch32rv_memmove.c and ch32rv_memset.c. GCC would
 * turn their loops back into calls to these very functions, so they are built
 * without that pattern. */
#pragma once

#define CH32RV_STRING_FN __attribute__((optimize("no-tree-loop-distribute-patterns")))
