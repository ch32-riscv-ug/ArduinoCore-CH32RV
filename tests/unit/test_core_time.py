"""The wall clock in cores/arduino/syscalls.c (gettimeofday / settimeofday /
clock), on the host, across millis()'s 32-bit wrap at 49.7 days.

syscalls.c is compiled with the host C compiler and every symbol in the object
gets a prefix (objcopy --prefix-symbols), so its _gettimeofday, _exit and
errno do not meet the host libc's. The driver supplies the prefixed millis()
and serial hook, steps the fake millis() across the wrap and prints what the
clock says; the expected values are written out here.
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
SRC = REPO / "cores/arduino/syscalls.c"

DRIVER = r"""
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#include <sys/time.h>

static uint32_t fake_ms;
int core_errno;
unsigned long core_millis(void) { return fake_ms; }
size_t core_ch32rv_serial_write_bytes(const unsigned char *d, size_t n) { (void)d; return n; }
int core__gettimeofday(struct timeval *tv, void *tz);
int core_settimeofday(const struct timeval *tv, const void *tz);

static long long now_ms(void) {
    struct timeval tv;
    core__gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int main(void) {
    fake_ms = 1000;
    printf("start=%lld\n", now_ms());                       /* uptime since 1970 */
    struct timeval t = { 1800000000, 250000 };               /* 2027-01-15, .25 s */
    core_settimeofday(&t, NULL);
    printf("set=%lld\n", now_ms());
    fake_ms = 0xFFFFF000u;                                   /* 49.7 days later, just before the wrap */
    long long before = now_ms();
    fake_ms = 0x00000800u;                                   /* 6144 ms later, after it */
    long long after = now_ms();
    printf("across=%lld\n", after - before);
    fake_ms = 0xFFFFF000u;                                   /* a second wrap */
    before = now_ms();
    fake_ms = 0x00000100u;
    after = now_ms();
    printf("second=%lld\n", after - before);
    printf("since_set=%lld\n", after - 1800000000250LL);     /* two wraps of uptime after the set */
    return 0;
}
"""


@pytest.fixture(scope="module")
def results(tmp_path_factory):
    cc = shutil.which("cc") or shutil.which("gcc")
    objcopy = shutil.which("objcopy")
    if not cc or not objcopy:
        pytest.skip("no host C compiler / objcopy")
    work = tmp_path_factory.mktemp("time")
    obj = work / "syscalls.o"
    # newlib's headers bring CLOCKS_PER_SEC in by the way; the host's need time.h.
    subprocess.run([cc, "-std=gnu11", "-c", "-O1", "-include", "time.h", str(SRC), "-o", str(obj)], check=True)
    subprocess.run([objcopy, "--prefix-symbols=core_", str(obj)], check=True)
    (work / "driver.c").write_text(DRIVER)
    exe = work / "driver"
    subprocess.run([cc, "-std=gnu11", str(work / "driver.c"), str(obj), "-o", str(exe)], check=True)
    out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
    return dict(line.split("=", 1) for line in out.splitlines())


@pytest.mark.parametrize("name,expected", [
    ("start", "1000"),                 # before settimeofday(): 1970 plus the uptime
    ("set", "1800000000250"),          # exactly what was set
    ("across", "6144"),                # 0xFFFFF000 -> 0x800 is 4096 + 2048 ms, not minus 4 billion
    ("second", "4352"),                # 4096 + 256 at the next wrap
    ("since_set", str(2 * (1 << 32) + 0x100 - 1000)),   # two wraps of 2^32 ms, from millis() 1000 to 0x100
])
def test_wall_clock_across_the_wrap(results, name, expected):
    assert results[name] == expected
