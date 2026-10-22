"""CH32RVTaskKit (libraries/CH32RVTaskKit), on the host.

Compiled with the host C++ compiler against the same stand-in ch32rv_irq.h as
test_synckit.py, with millis() / micros() driven by the driver. Each scenario
prints the order the works ran in (or a number); the expected values are
written out here.
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
TASK = REPO / "libraries/CH32RVTaskKit/src"
SYNC = REPO / "libraries/CH32RVSyncKit/src"

IRQ_STUB = r"""
#pragma once
#include <stdint.h>
static inline uint32_t ch32rv_irq_save(void) { return 0; }
static inline void ch32rv_irq_restore(uint32_t) {}
"""

DRIVER = r"""
#include <stdio.h>
#include <string.h>
#include "CH32RVTaskKit.h"
using namespace CH32RVTaskKit;

static unsigned long clock_ms, clock_us;
extern "C" unsigned long millis(void) { return clock_ms; }
extern "C" unsigned long micros(void) { return clock_us; }

static char log_[256];
static void mark(const char *s) { strcat(log_, s); }
static void show(const char *name) { printf("%s=%s\n", name, log_); log_[0] = 0; }

static Executor app, other;
static int mode;
static Work *selfp;
static void a() { mark("a"); if (mode == 1) app.attach(*selfp); }
static void b() { mark("b"); if (mode == 2) selfp->post(); }
static void c() { mark("c"); clock_us += 250; }
static void nest() { mark("n"); char r[8]; snprintf(r, sizeof r, "%u", (unsigned)app.runOnce()); mark(r);
                     snprintf(r, sizeof r, "%u", (unsigned)other.runOnce()); mark(r); }
static Work wa(a), wb(b), wc(c), wn(nest);
static void ctx(void *p) { mark((const char *)p); }
static Work wx(ctx, (void *)"x");
static void quitter() { mark("q"); app.detach(*selfp); }
static Work wq(quitter);

static bool flag;
static bool cond() { return flag; }

int main() {
    printf("empty=%u\n", (unsigned)app.runOnce());

    app.attach(wb); app.attach(wa); app.attach(wc);
    printf("attached_ready=%d\n", wa.ready());
    app.runOnce(); show("first");              /* every attached work once, in attach order */
    app.runOnce(); show("second");             /* nothing ready: nothing */
    wa.post(); wa.post(); wc.post(); app.runOnce(); show("posted");   /* two posts, one run */

    selfp = &wb; mode = 2; wb.post(); app.runOnce(); show("self_post");   /* b posts itself: once now */
    mode = 0; app.runOnce(); show("self_post_next");                      /* and once in the next round */

    printf("twice=%d other_refused=%d\n", app.attach(wa), other.attach(wa));

    app.attach(wn); app.runOnce(); show("nested");   /* n's nested rounds (own and other's) return 0 */

    app.attach(wx); app.runOnce(); show("context");

    selfp = &wq; app.attach(wq); wb.post(); app.runOnce(); show("quit1");   /* q detaches itself, b still runs */
    wq.post(); app.runOnce(); show("quit2"); printf("quit_attached=%d\n", wq.attached());

    /* triggers */
    clock_ms = 1000;
    CH32RVSyncKit::Deadline d; d.arm(1000, 100); wa.wakeOn(d);
    CH32RVSyncKit::Period p(50); wb.wakeEvery(p); p.start(1000);
    wc.wakeWhen(cond);
    app.runOnce(); show("t0");
    clock_ms = 1050; app.runOnce(); show("t50");
    clock_ms = 1100; app.runOnce(); show("t100");
    flag = true; app.runOnce(); show("t100_flag");
    d.disarm(); flag = false; clock_ms = 1120; app.runOnce(); show("t120");

    /* stats */
    WorkStats s; RoundStats r; wc.wakeWhen(nullptr); wc.measure(s); app.measure(r);
    wc.post(); app.runOnce(); wc.post(); app.runOnce(); app.runOnce();
    printf("runs=%u total=%u max=%u rounds=%u empty=%u\n", (unsigned)s.runs, (unsigned)s.totalUs,
           (unsigned)s.maxUs, (unsigned)r.rounds, (unsigned)r.emptyRounds);
    return 0;
}
"""

EXPECT = {
    "empty": "0", "attached_ready": "1", "first": "bac", "second": "", "posted": "ac",
    "self_post": "b", "self_post_next": "b", "twice": "1 other_refused=0", "nested": "n00",
    "context": "x", "quit1": "bq", "quit2": "", "quit_attached": "0",
    "t0": "", "t50": "b", "t100": "ba", "t100_flag": "ac", "t120": "",
    "runs": "2 total=500 max=250 rounds=3 empty=1",
}


@pytest.fixture(scope="module")
def results(tmp_path_factory):
    cxx = shutil.which("c++") or shutil.which("g++")
    if not cxx:
        pytest.skip("no host C++ compiler")
    work = tmp_path_factory.mktemp("taskkit")
    (work / "ch32rv_irq.h").write_text(IRQ_STUB)
    (work / "driver.cpp").write_text(DRIVER)
    exe = work / "driver"
    subprocess.run([cxx, "-std=gnu++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    f"-I{work}", f"-I{TASK}", f"-I{SYNC}", str(work / "driver.cpp"), "-o", str(exe)], check=True)
    out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
    return dict(line.split("=", 1) for line in out.splitlines())


@pytest.mark.parametrize("name", sorted(EXPECT))
def test_taskkit(results, name):
    assert results[name] == EXPECT[name]
