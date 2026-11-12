/* The system calls picolibc leaves to the program.
 *
 * printf()/puts() do not come here: the core prints through its own formatter
 * (ch32rv_stdio_out.c), and stdio's FILEs are in ch32rv_stdio_file.c. What is
 * here is answered without an operating system: write() to the monitor port,
 * read() at end of file, and the file system calls (open(), unlink(), stat()
 * and the rest, which fopen() and remove() reach) failing with ENOSYS rather
 * than as a link error, so a sketch that pulls in an unexpected libc corner
 * fails predictably at run time.
 *
 * sbrk() is deliberately *not* here - see ch32rv_sbrk.c for why.
 */
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

unsigned long millis(void);

#include "internal/ch32rv_rtc.h"
#include "internal/ch32rv_serial_write.h"

ssize_t write(int fd, const void *buf, size_t count)
{
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        errno = EBADF;
        return -1;
    }
    return (ssize_t)ch32rv_serial_write_bytes((const uint8_t *)buf, count);
}

ssize_t read(int fd, void *buf, size_t count)
{
    (void)fd; (void)buf; (void)count;
    return 0;   /* EOF */
}

int close(int fd)              { (void)fd; errno = EBADF; return -1; }
int fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    st->st_blksize = 64;
    return 0;
}
int isatty(int fd)             { (void)fd; return 1; }
off_t lseek(int fd, off_t off, int whence) { (void)fd; (void)off; (void)whence; return 0; }

int open(const char *path, int flags, ...) { (void)path; (void)flags; errno = ENOSYS; return -1; }
int unlink(const char *path)   { (void)path; errno = ENOSYS; return -1; }
int link(const char *a, const char *b) { (void)a; (void)b; errno = ENOSYS; return -1; }
int stat(const char *path, struct stat *st) { (void)path; (void)st; errno = ENOSYS; return -1; }

/* The wall clock. Where there is an RTC (ch32rv_rtc.h) it is the RTC, which
 * keeps counting through resets and sleep. Elsewhere: milliseconds since
 * reset, widened to 64 bits, plus the offset settimeofday() sets (an SNTP
 * client). It
 * reads 1970-01-01 plus the uptime until then. The widening notices the 32-bit
 * millis() wrapping each time it is read, so the clock must be read at least
 * once every 49.7 days (an SNTP client resyncing does). Task context only;
 * the resolution is one millisecond. */
static uint64_t wall_offset_ms;
static uint64_t uptime_high;
static uint32_t uptime_last;

static uint64_t uptime_ms(void)
{
    const uint32_t now = (uint32_t)millis();
    if (now < uptime_last) {
        uptime_high += 1ull << 32;
    }
    uptime_last = now;
    return uptime_high | now;
}

/* time() reads it. */
int gettimeofday(struct timeval *restrict tv, void *restrict tz)
{
    (void)tz;
#ifdef CH32RV_HAS_RTC
    /* The RTC is the clock here: it keeps counting through resets and sleep. */
    if (tv && ch32rv_rtc_begin()) {
        uint32_t sec, ms;
        ch32rv_rtc_get(&sec, &ms);
        tv->tv_sec = (time_t)sec;
        tv->tv_usec = (suseconds_t)(ms * 1000u);
        return 0;
    }
#endif
    if (tv) {
        const uint64_t ms = uptime_ms() + wall_offset_ms;
        tv->tv_sec = (time_t)(ms / 1000u);
        tv->tv_usec = (suseconds_t)(ms % 1000u * 1000u);
    }
    return 0;
}

int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
    (void)tz;
    if (!tv || tv->tv_sec < 0 || tv->tv_usec < 0 || tv->tv_usec >= 1000000) {
        errno = EINVAL;
        return -1;
    }
#ifdef CH32RV_HAS_RTC
    if (ch32rv_rtc_begin()) {
        ch32rv_rtc_set((uint32_t)tv->tv_sec, (uint32_t)tv->tv_usec / 1000u);
        return 0;
    }
#endif
    const uint64_t ms = (uint64_t)tv->tv_sec * 1000u + (uint64_t)tv->tv_usec / 1000u;
    wall_offset_ms = ms - uptime_ms();
    return 0;
}

/* clock() reads it: CPU time is the time since reset here. */
clock_t times(struct tms *buf)
{
    const clock_t ticks = (clock_t)(uptime_ms() * CLOCKS_PER_SEC / 1000u);
    if (buf) {
        buf->tms_utime = ticks;
        buf->tms_stime = buf->tms_cutime = buf->tms_cstime = 0;
    }
    return ticks;
}

void _exit(int code)
{
    (void)code;
    for (;;) {
    }
}
