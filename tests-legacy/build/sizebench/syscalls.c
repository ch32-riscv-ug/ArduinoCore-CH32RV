/* Minimal newlib syscall stubs for size measurement (never executed). */
#include <stdint.h>
#include <sys/stat.h>

void SystemInit(void) {}

volatile uint8_t sizebench_sink;

int _write(int fd, const char *buf, int len)
{
    (void)fd;
    for (int i = 0; i < len; i++) sizebench_sink = (uint8_t)buf[i];
    return len;
}
int _read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return 0; }
int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _lseek(int fd, int off, int wh) { (void)fd; (void)off; (void)wh; return 0; }
void _exit(int code) { (void)code; for (;;); }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void) { return 1; }

extern char end[];
static char *heap = end;
void *_sbrk(int inc) { char *p = heap; heap += inc; return p; }
