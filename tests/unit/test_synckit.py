"""CH32RVSyncKit (libraries/CH32RVSyncKit), on the host.

The headers are compiled with the host C++ compiler against a stand-in
ch32rv_irq.h that records every save / restore, so the driver below can check
both the values each part hands back and that every IsrSafe operation masks
interrupts exactly once and puts them back (and that LoopOnly never does).
millis() is the driver's own clock. Each scenario prints name=value lines; the
expected values are written out here, independent of the implementation.
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
SRC = REPO / "libraries/CH32RVSyncKit/src"

IRQ_STUB = r"""
#pragma once
#include <stdint.h>
extern int irq_depth, irq_saves, irq_max_depth;
static inline uint32_t ch32rv_irq_save(void) {
    irq_saves++; irq_depth++; if (irq_depth > irq_max_depth) irq_max_depth = irq_depth; return irq_depth;
}
static inline void ch32rv_irq_restore(uint32_t) { irq_depth--; }
"""

DRIVER = r"""
#include <stdio.h>
#include <string.h>
#include "CH32RVSyncKit.h"
using namespace CH32RVSyncKit;

int irq_depth, irq_saves, irq_max_depth;
static unsigned long clock_ms;
extern "C" unsigned long millis(void) { return clock_ms; }

#define OUT(name, v) printf("%s=%ld\n", name, (long)(v))
#define GUARDS(name, expr) do { int s0 = irq_saves; expr; OUT(name, irq_saves - s0); } while (0)

int main() {
    /* Queue: FIFO, full refuses, front insert, peek, clear, guards */
    {
        Queue<int, 3> q; int v = -1;
        OUT("q_empty_rx", q.tryReceive(v)); OUT("q_empty_v", v);
        OUT("q_s1", q.trySend(1)); OUT("q_s2", q.trySend(2)); OUT("q_s3", q.trySend(3));
        OUT("q_full", q.trySend(4)); OUT("q_count", q.count()); OUT("q_isfull", q.isFull());
        q.tryReceive(v); OUT("q_r1", v);
        OUT("q_front", q.trySendToFront(9));
        q.tryPeek(v); OUT("q_peek", v); OUT("q_peek_count", q.count());
        q.tryReceive(v); OUT("q_r2", v); q.tryReceive(v); OUT("q_r3", v); q.tryReceive(v); OUT("q_r4", v);
        OUT("q_after", q.isEmpty());
        for (int i = 0; i < 7; i++) { q.trySend(i); q.tryReceive(v); }
        OUT("q_wrap_v", v);
        q.trySend(5); q.clear(); OUT("q_cleared", q.count());
        GUARDS("q_guard_send", q.trySend(1));
        GUARDS("q_guard_recv", q.tryReceive(v));
        OUT("q_depth_after", irq_depth);
        Queue<int, 1> box; box.overwrite(1); box.overwrite(2); box.tryReceive(v); OUT("box", v);
        Queue<int, 2, LoopOnly> lq; GUARDS("q_loop_guard", lq.trySend(1));
    }
    /* Notify counter and bits, BinarySemaphore */
    {
        Notify<> n;
        OUT("n_take0", n.tryTake()); n.notify(); n.notify(); n.notify();
        OUT("n_take1", n.tryTake()); OUT("n_all", n.tryTakeAll()); OUT("n_all2", n.tryTakeAll());
        n.setBits(0x1); OUT("n_any", n.tryWaitBits(0x3)); OUT("n_after", n.bits());
        n.setBits(0x1); OUT("n_all_wait", n.tryWaitBits(0x3, true, true)); OUT("n_kept", n.bits());
        n.setBits(0x2); OUT("n_all_ok", n.tryWaitBits(0x3, true, true)); OUT("n_cleared", n.bits());
        n.setBits(0x4); OUT("n_peek", n.tryWaitBits(0x4, false)); OUT("n_peek_kept", n.bits());
        BinarySemaphore<> b;
        OUT("b_take0", b.tryTake()); OUT("b_give1", b.give()); OUT("b_give2", b.give());
        OUT("b_take1", b.tryTake()); OUT("b_take2", b.tryTake());
    }
    /* Ring: partial write, wrap, byte API */
    {
        Ring<8> r; uint8_t buf[16];
        OUT("r_w1", r.write("abcdef", 6)); OUT("r_w2", r.write("ghijk", 5)); OUT("r_avail", r.available());
        OUT("r_rd", r.read(buf, 4)); buf[4] = 0; printf("r_text=%s\n", buf);
        OUT("r_w3", r.write("XYZW", 4)); size_t n = r.read(buf, 16); buf[n] = 0; printf("r_wrap=%s\n", buf);
        OUT("r_empty", r.read()); r.write((uint8_t)'q'); OUT("r_peek", r.peek()); OUT("r_byte", r.read());
    }
    /* MessageBuffer: whole or nothing, too small, empty message, wrap */
    {
        MessageBuffer<12> m; char out[16];
        OUT("m_max", m.maxMessage());
        OUT("m_s1", m.trySend("abc", 3)); OUT("m_s2", m.trySend("defgh", 5)); OUT("m_s3", m.trySend("x", 1));
        OUT("m_count", m.count()); OUT("m_next", m.nextLength());
        OUT("m_small", m.tryReceive(out, 2)); OUT("m_still", m.count());
        size_t k = m.tryReceive(out, 16); out[k] = 0; printf("m_r1=%s\n", out);
        OUT("m_s4", m.trySend("", 0)); OUT("m_s5", m.trySend("12", 2));
        k = m.tryReceive(out, 16); out[k] = 0; printf("m_r2=%s\n", out);
        OUT("m_r3", m.tryReceive(out, 16)); OUT("m_r3_count", m.count());
        k = m.tryReceive(out, 16); out[k] = 0; printf("m_r4=%s\n", out);
        OUT("m_too_long", m.trySend("0123456789ab", 12));
    }
    /* BlockPool: exhaust, refuse foreign / double release, more than 32 blocks */
    {
        BlockPool<int, 3> p; int *a = p.tryAllocate(), *b = p.tryAllocate(), *c = p.tryAllocate();
        OUT("p_three", a && b && c && a != b && b != c); OUT("p_fourth", p.tryAllocate() == nullptr);
        int foreign; OUT("p_foreign", p.release(&foreign)); OUT("p_rel", p.release(b)); OUT("p_double", p.release(b));
        OUT("p_avail", p.available()); OUT("p_again", p.tryAllocate() == b);
        BlockPool<uint8_t, 40> big; int got = 0; while (big.tryAllocate()) got++; OUT("p_big", got);
    }
    /* TryLock and its Guard */
    {
        TryLock<> l; OUT("l_first", l.tryLock()); OUT("l_second", l.tryLock());
        { TryLock<>::Guard g(l); OUT("l_guard_held", g.locked()); }
        OUT("l_still", l.locked()); l.unlock();
        { TryLock<>::Guard g(l); OUT("l_guard_got", g.locked()); OUT("l_inside", l.locked()); }
        OUT("l_released", l.locked());
    }
    /* CriticalSection nests and restores */
    {
        { CriticalSection a; { CriticalSection b; OUT("cs_inner", irq_depth); } OUT("cs_outer", irq_depth); }
        OUT("cs_after", irq_depth);
    }
    /* Deadline and Period, including wrap of the 32-bit clock */
    {
        clock_ms = 1000; Deadline d; OUT("d_unarmed", d.expired());
        d.arm(500); OUT("d_early", d.expired()); OUT("d_left", d.remaining());
        clock_ms = 1500; OUT("d_due", d.expired()); OUT("d_left0", d.remaining());
        clock_ms = 0xFFFFFF00ul; d.arm(0x200); clock_ms = 0xFFFFFFFFul; OUT("d_wrap_early", d.expired());
        clock_ms = 0x100; OUT("d_wrap_due", d.expired());
        d.disarm(); OUT("d_disarmed", d.expired());

        clock_ms = 0; Period p(100);
        OUT("p0", p.due()); clock_ms = 99; OUT("p99", p.due()); clock_ms = 100; OUT("p100", p.due());
        clock_ms = 150; OUT("p150", p.due()); clock_ms = 200; OUT("p200", p.due());
        clock_ms = 650; OUT("p650", p.due()); OUT("p_missed", p.missed());
        clock_ms = 699; OUT("p699", p.due()); clock_ms = 700; OUT("p700", p.due());
        clock_ms = 0xFFFFFFF0ul; p.start(); clock_ms = 0x54; OUT("p_wrap", p.due());
    }
    OUT("depth_end", irq_depth);
    return 0;
}
"""

EXPECT = {
    "q_empty_rx": 0, "q_empty_v": -1, "q_s1": 1, "q_s2": 1, "q_s3": 1, "q_full": 0, "q_count": 3,
    "q_isfull": 1, "q_r1": 1, "q_front": 1, "q_peek": 9, "q_peek_count": 3, "q_r2": 9, "q_r3": 2,
    "q_r4": 3, "q_after": 1, "q_wrap_v": 6, "q_cleared": 0, "q_guard_send": 1, "q_guard_recv": 1,
    "q_depth_after": 0, "box": 2, "q_loop_guard": 0,
    "n_take0": 0, "n_take1": 1, "n_all": 2, "n_all2": 0, "n_any": 1, "n_after": 0, "n_all_wait": 0,
    "n_kept": 1, "n_all_ok": 3, "n_cleared": 0, "n_peek": 4, "n_peek_kept": 4,
    "b_take0": 0, "b_give1": 1, "b_give2": 0, "b_take1": 1, "b_take2": 0,
    "r_w1": 6, "r_w2": 2, "r_avail": 8, "r_rd": 4, "r_text": "abcd", "r_w3": 4, "r_wrap": "efghXYZW",
    "r_empty": -1, "r_peek": ord("q"), "r_byte": ord("q"),
    "m_max": 10, "m_s1": 1, "m_s2": 1, "m_s3": 0, "m_count": 2, "m_next": 3, "m_small": 0, "m_still": 2,
    "m_r1": "abc", "m_s4": 1, "m_s5": 0, "m_r2": "defgh", "m_r3": 0, "m_r3_count": 0, "m_r4": "",
    "m_too_long": 0,
    "p_three": 1, "p_fourth": 1, "p_foreign": 0, "p_rel": 1, "p_double": 0, "p_avail": 1, "p_again": 1,
    "p_big": 40,
    "l_first": 1, "l_second": 0, "l_guard_held": 0, "l_still": 1, "l_guard_got": 1, "l_inside": 1,
    "l_released": 0,
    "cs_inner": 2, "cs_outer": 1, "cs_after": 0,
    "d_unarmed": 0, "d_early": 0, "d_left": 500, "d_due": 1, "d_left0": 0, "d_wrap_early": 0,
    "d_wrap_due": 1, "d_disarmed": 0,
    "p0": 0, "p99": 0, "p100": 1, "p150": 0, "p200": 1, "p650": 1, "p_missed": 3, "p699": 0, "p700": 1,
    "p_wrap": 1,
    "depth_end": 0,
}


@pytest.fixture(scope="module")
def results(tmp_path_factory):
    cxx = shutil.which("c++") or shutil.which("g++")
    if not cxx:
        pytest.skip("no host C++ compiler")
    work = tmp_path_factory.mktemp("synckit")
    (work / "ch32rv_irq.h").write_text(IRQ_STUB)
    (work / "driver.cpp").write_text(DRIVER)
    exe = work / "driver"
    subprocess.run([cxx, "-std=gnu++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    f"-I{work}", f"-I{SRC}", str(work / "driver.cpp"), "-o", str(exe)], check=True)
    out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
    return dict(line.split("=", 1) for line in out.splitlines())


@pytest.mark.parametrize("name", sorted(EXPECT))
def test_synckit(results, name):
    assert results[name] == str(EXPECT[name])


def test_every_line_expected(results):
    assert set(results) == set(EXPECT)
