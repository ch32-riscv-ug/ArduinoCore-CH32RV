/* How close the heap may come to the stack.
 *
 * sbrk (ch32rvsbrk.c) never moves the heap's top past _heap_end, the bottom
 * of the stack the linker reserves (sections.ld, 512 B). Code that goes deeper
 * than that reserve walks down over whatever the heap holds there, silently:
 * TLS's handshake takes 4.4 KB of stack or more, and on a V307 at 224k_96k it
 * overwrote the session's own keys, which surfaced as a MAC error on the first
 * record (2026-11-03). A library whose code goes that deep raises this margin
 * while it runs: sbrk then also refuses to bring the heap's top within this
 * many bytes of the current stack pointer, and the allocation fails with
 * ENOMEM instead of being written over later. 0, the default, keeps only the
 * linker's reserve. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern size_t ch32rv_heap_stack_margin;

#ifdef __cplusplus
}
#endif
