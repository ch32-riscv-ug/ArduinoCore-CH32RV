/* Minimal newlib syscall stubs.
 *
 * printf()/puts() reach the serial monitor through _write; everything else is
 * a well-behaved failure rather than a link error, so a sketch that pulls in
 * an unexpected libc corner fails predictably instead of silently.
 *
 * These have to beat the libgloss versions, which are *semihosting* stubs: they
 * issue `ecall`, and a CH32 with no environment-call handler traps that to the
 * reset vector. libgloss is scanned after core.a, so plain left-to-right
 * resolution picked its _write and printf() rebooted the board. platform.txt
 * links core.a inside --start-group/--end-group, which makes the linker come
 * back to this object when libc asks for _write.
 *
 * _sbrk is deliberately *not* here - see ch32_sbrk.c for why.
 */
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ch32_serial_write.h"

#undef errno
extern int errno;

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

int _close(int fd)             { (void)fd; errno = EBADF; return -1; }
/* st_blksize matters: newlib's __swhatbuf_r reads it to size the buffer it
 * mallocs for stdout, and leaving it unset means that size comes from whatever
 * was on the stack. */
int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    st->st_blksize = 64;
    return 0;
}
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
