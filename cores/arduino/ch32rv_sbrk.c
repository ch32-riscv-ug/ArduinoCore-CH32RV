/* The heap's only syscall, deliberately in a file of its own.
 *
 * It has to be separate from syscalls.c because of how the linker pulls
 * archive members. Everything in cores/arduino ends up in core.a, and a member
 * is only extracted when something already needs a symbol it defines. Put
 * _sbrk next to _write and any sketch that allocates - `new`, String, malloc -
 * drags the whole HardwareSerial driver in behind _write, which costs ~1.7 KB
 * on a part that may only have 16 KB of flash. Alone in here it costs its own
 * few dozen bytes.
 *
 * Getting *our* _sbrk linked at all matters more than its size: libgloss ships
 * a semihosting one that issues `ecall`, and on a CH32 with no environment-call
 * handler that traps to the reset vector. A sketch with a global String used to
 * reboot in a loop and print nothing whatsoever. platform.txt now links core.a
 * inside --start-group/--end-group so the archive is rescanned when libc asks
 * for _sbrk, which is what makes this definition win over libgloss's.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

#undef errno
extern int errno;

extern char _end[];        /* linker script: first byte above .bss */
extern char _heap_end[];   /* linker script: bottom of the reserved stack */

void *_sbrk(ptrdiff_t incr)
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
    brk = next;
    return prev;
}
