/* Minimal newlib syscall stubs.
 *
 * printf()/puts() reach the serial monitor through _write; everything else is
 * a well-behaved failure rather than a link error, so a sketch that pulls in
 * an unexpected libc corner fails predictably instead of silently.
 */
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ch32_serial_write.h"

#undef errno
extern int errno;

extern char _end[];        /* provided by the linker script */
extern char _eusrstack[];

ssize_t _write(int fd, const void *buf, size_t count)
{
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        errno = EBADF;
        return -1;
    }
    return (ssize_t)ch32_serial_write_bytes((const uint8_t *)buf, count);
}

ssize_t _read(int fd, void *buf, size_t count)
{
    (void)fd; (void)buf; (void)count;
    return 0;   /* EOF */
}

/* The stack grows down from the top of RAM and the heap grows up from _end;
 * refuse to let them meet. */
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
    if (next > _eusrstack || next < _end) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk = next;
    return prev;
}

int _close(int fd)             { (void)fd; errno = EBADF; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)            { (void)fd; return 1; }
off_t _lseek(int fd, off_t off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _getpid(void)              { return 1; }
int _kill(int pid, int sig)    { (void)pid; (void)sig; errno = EINVAL; return -1; }

void _exit(int code)
{
    (void)code;
    for (;;) {
    }
}
