"""The system-service round (cores/arduino/ch32rv_service.c), on the host.

delay() and the return of loop() run every attached service once, in attach
order, never nested; a service may detach itself (or another one) from inside
the round. The C file knows no registers, so the host compiler builds it with
a small driver; each scenario prints the order the services ran in, and the
expected order is written out here.
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
SRC = REPO / "cores/arduino/ch32rv_service.c"

DRIVER = r"""
#include <stdio.h>
#include <string.h>
#include "ch32rv_service.h"

static char log_[256];
static void mark(const char *s) { strcat(log_, s); }

static ch32rv_service_t a, b, c;
static int mode;

static void run_a(void) {
    mark("a");
    if (mode == 1) ch32rv_services_run();          /* a service that ends up in delay() */
    if (mode == 2) ch32rv_service_detach(&a);      /* ends itself */
    if (mode == 3) ch32rv_service_detach(&b);      /* ends the next one */
    if (mode == 4) ch32rv_service_attach(&a);      /* attaches itself again: nothing */
}
static void run_b(void) { mark("b"); if (mode == 5) ch32rv_service_detach(&c); }
static void run_c(void) { mark("c"); }

static void reset(void) {
    ch32rv_service_detach(&a); ch32rv_service_detach(&b); ch32rv_service_detach(&c);
    log_[0] = 0; mode = 0;
}

static void round_(const char *label) {
    ch32rv_services_run();
    printf("%s=%s\n", label, log_);
    log_[0] = 0;
}

int main(void) {
    a = (ch32rv_service_t)CH32RV_SERVICE_INIT(run_a);
    b = (ch32rv_service_t)CH32RV_SERVICE_INIT(run_b);
    c = (ch32rv_service_t)CH32RV_SERVICE_INIT(run_c);

    round_("empty");

    reset(); ch32rv_service_attach(&b); ch32rv_service_attach(&a); ch32rv_service_attach(&c);
    round_("order");
    ch32rv_service_attach(&a);                     /* already attached */
    round_("twice");

    reset(); ch32rv_service_attach(&a); ch32rv_service_attach(&b); mode = 1;
    round_("nested");

    reset(); ch32rv_service_attach(&a); ch32rv_service_attach(&b); mode = 2;
    round_("self1"); mode = 0; round_("self2");

    reset(); ch32rv_service_attach(&a); ch32rv_service_attach(&b); ch32rv_service_attach(&c); mode = 3;
    round_("next1"); mode = 0; round_("next2");

    reset(); ch32rv_service_attach(&a); ch32rv_service_attach(&b); mode = 4;
    round_("reattach");

    reset(); ch32rv_service_attach(&a); ch32rv_service_attach(&b); ch32rv_service_attach(&c); mode = 5;
    round_("tail1"); mode = 0; round_("tail2");

    reset(); ch32rv_service_detach(&a);            /* not attached: nothing */
    ch32rv_service_attach(&c); ch32rv_service_detach(&c); ch32rv_service_attach(&c);
    round_("again");
    return 0;
}
"""


@pytest.fixture(scope="module")
def rounds(tmp_path_factory):
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no host C compiler")
    work = tmp_path_factory.mktemp("service")
    (work / "driver.c").write_text(DRIVER)
    exe = work / "driver"
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    f"-I{SRC.parent}", str(SRC), str(work / "driver.c"), "-o", str(exe)], check=True)
    out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
    return dict(line.split("=", 1) for line in out.splitlines())


@pytest.mark.parametrize("label,ran", [
    ("empty", ""),            # nothing attached: the round does nothing
    ("order", "bac"),         # attach order, each once
    ("twice", "bac"),         # attaching an attached service changes nothing
    ("nested", "ab"),         # a round from inside a service does not start: a once, then b
    ("self1", "ab"),          # a detaches itself; the round still reaches b
    ("self2", "b"),           # and a is gone afterwards
    ("next1", "ac"),          # a detaches b before b's turn: b is skipped, c still runs
    ("next2", "ac"),
    ("reattach", "ab"),       # a attaching itself again from inside the round: no change
    ("tail1", "ab"),          # b detaches the last one before its turn
    ("tail2", "ab"),
    ("again", "c"),           # detach of an unattached one is ignored; attach after detach works
])
def test_round(rounds, label, ran):
    assert rounds[label] == ran
