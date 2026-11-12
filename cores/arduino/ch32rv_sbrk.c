/* The heap's only syscall, deliberately in a file of its own.
 *
 * It has to be separate from syscalls.c because of how the linker pulls
 * archive members. Everything in cores/arduino ends up in core.a, and a member
 * is only extracted when something already needs a symbol it defines. Put
 * sbrk next to write and any sketch that allocates - `new`, String, malloc -
 * drags the whole HardwareSerial driver in behind write, which costs ~1.7 KB
 * on a part that may only have 16 KB of flash. Alone in here it costs its own
 * few dozen bytes.
 *
 * picolibc has a fallback sbrk() of its own (weak, over __heap_start and
 * __heap_end); this one replaces it, with the stack margin below.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

#include "internal/ch32rv_heap.h"

extern char _end[];        /* linker script: first byte above .bss */
extern char _heap_end[];   /* linker script: bottom of the reserved stack */

size_t ch32rv_heap_stack_margin;   /* internal/ch32rv_heap.h */

void *sbrk(ptrdiff_t incr)
{
    static char *brk;
    char *prev;
    char *next;

    if (brk == 0) {
        brk = _end;
    }
    prev = brk;
    next = brk + incr;
    /* Stopping at _heap_end rather than the top of RAM keeps the reserved
     * stack out of reach, so a runaway allocation fails instead of quietly
     * scribbling over the stack of the code that asked for it. */
    if (next > _heap_end || next < _end) {
        errno = ENOMEM;
        return (void *)-1;
    }
    if (incr > 0 && ch32rv_heap_stack_margin) {
        char *sp;
        __asm__ volatile("mv %0, sp" : "=r"(sp));
        if (next > sp - ch32rv_heap_stack_margin) {
            errno = ENOMEM;
            return (void *)-1;
        }
    }
    brk = next;
    return prev;
}
