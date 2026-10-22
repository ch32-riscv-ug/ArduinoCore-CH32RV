"""The delivery of notification callbacks (cores/arduino/ch32rv_deliver.c), on the host.

deliverEvents() runs every attached deliverer once, in attach order, returns
the sum of the callbacks they ran, and does nothing from inside one (a
callback that calls it again gets 0). Each scenario prints the order the
deliverers ran in and the count; the expected values are written out here.
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
SRC = REPO / "cores/arduino/ch32rv_deliver.c"

DRIVER = r"""
#include <stdio.h>
#include <string.h>
#include "ch32rv_deliver.h"

static char log_[256];
static void mark(const char *s) { strcat(log_, s); }

static ch32rv_delivery_t a, b, c;
static int mode;
static size_t inner;

static size_t run_a(void) {
    mark("a");
    if (mode == 1) inner = deliverEvents();          /* a callback that delivers again */
    if (mode == 2) ch32rv_delivery_detach(&a);       /* ends itself */
    if (mode == 3) ch32rv_delivery_detach(&b);       /* ends the next one */
    return 2;
}
static size_t run_b(void) { mark("b"); return 0; }
static size_t run_c(void) { mark("c"); return 1; }

static void reset(void) {
    ch32rv_delivery_detach(&a); ch32rv_delivery_detach(&b); ch32rv_delivery_detach(&c);
    log_[0] = 0; mode = 0; inner = 99;
}

static void deliver(const char *label) {
    const size_t n = deliverEvents();
    printf("%s=%s %u\n", label, log_, (unsigned)n);
    log_[0] = 0;
}

int main(void) {
    a = (ch32rv_delivery_t)CH32RV_DELIVERY_INIT(run_a);
    b = (ch32rv_delivery_t)CH32RV_DELIVERY_INIT(run_b);
    c = (ch32rv_delivery_t)CH32RV_DELIVERY_INIT(run_c);

    deliver("empty");

    reset(); ch32rv_delivery_attach(&c); ch32rv_delivery_attach(&a); ch32rv_delivery_attach(&b);
    deliver("order");
    ch32rv_delivery_attach(&a);
    deliver("twice");

    reset(); ch32rv_delivery_attach(&a); ch32rv_delivery_attach(&c); mode = 1;
    deliver("nested"); printf("inner=%u\n", (unsigned)inner);

    reset(); ch32rv_delivery_attach(&a); ch32rv_delivery_attach(&c); mode = 2;
    deliver("self1"); mode = 0; deliver("self2");

    reset(); ch32rv_delivery_attach(&a); ch32rv_delivery_attach(&b); ch32rv_delivery_attach(&c); mode = 3;
    deliver("next1"); mode = 0; deliver("next2");
    return 0;
}
"""


@pytest.fixture(scope="module")
def results(tmp_path_factory):
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no host C compiler")
    work = tmp_path_factory.mktemp("deliver")
    (work / "driver.c").write_text(DRIVER)
    exe = work / "driver"
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    f"-I{SRC.parent}", str(SRC), str(work / "driver.c"), "-o", str(exe)], check=True)
    out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
    return dict(line.split("=", 1) for line in out.splitlines())


@pytest.mark.parametrize("label,expected", [
    ("empty", " 0"),          # nothing attached: nothing runs
    ("order", "cab 3"),       # attach order, each once; the counts add up
    ("twice", "cab 3"),       # attaching an attached one changes nothing
    ("nested", "ac 3"),       # a's inner delivery does not start ...
    ("inner", "0"),           # ... and returns 0
    ("self1", "ac 3"),        # a detaches itself; c still runs
    ("self2", "c 1"),
    ("next1", "ac 3"),        # a detaches b before its turn
    ("next2", "ac 3"),
])
def test_deliver(results, label, expected):
    assert results[label] == expected
