"""CH32RVCallback (libraries/CH32RVUsbDevice/src/CH32RVUsbCallback.h), built and run on the host.

Function pointers, a [this] lambda and a two-pointer capture are called through it; an empty one
is false; a capture bigger than two pointers, or one that is not trivially copyable, must not
compile.
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
INC = REPO / "libraries/CH32RVUsbDevice/src"

PROGRAM = r"""
#include "CH32RVUsbCallback.h"
#include <cstdio>
static int seen;
static void plain(int v) { seen = v; }
struct Owner {
    int base = 100;
    CH32RVCallback<void(int)> make() { return [this](int v) { seen = base + v; }; }
};
int main() {
    CH32RVCallback<void(int)> none;
    CH32RVCallback<void(int)> null = nullptr;
    if (none || null) return 1;
    CH32RVCallback<void(int)> f = plain;
    f(7); if (seen != 7) return 2;
    Owner o; CH32RVCallback<void(int)> g = o.make();
    g(5); if (seen != 105) return 3;
    int a = 1, b = 2; int *pa = &a, *pb = &b;
    CH32RVCallback<int(int)> h = [pa, pb](int v) { return *pa + *pb + v; };
    if (h(3) != 6) return 4;
    CH32RVCallback<bool(void)> k = []() { return true; };
    if (!k()) return 5;
    CH32RVCallback<int(int)> copy = h;
    if (copy(4) != 7) return 6;
    std::puts("ok");
    return 0;
}
"""

TOO_BIG = r"""
#include "CH32RVUsbCallback.h"
int main() {
    long long a = 1, b = 2, c = 3;
    CH32RVCallback<long long()> f = [a, b, c]() { return a + b + c; };
    return (int)f();
}
"""


@pytest.fixture(scope="module")
def cxx():
    c = shutil.which("c++") or shutil.which("g++")
    if not c:
        pytest.skip("no host C++ compiler")
    return c


def test_calls(cxx, tmp_path):
    src = tmp_path / "t.cpp"
    src.write_text(PROGRAM)
    exe = tmp_path / "t"
    subprocess.run([cxx, "-std=gnu++17", "-Wall", "-Wextra", "-Werror", f"-I{INC}", str(src), "-o", str(exe)],
                   check=True)
    out = subprocess.run([str(exe)], capture_output=True, text=True)
    assert out.returncode == 0, out.returncode
    assert out.stdout.strip() == "ok"


def test_big_capture_does_not_compile(cxx, tmp_path):
    src = tmp_path / "big.cpp"
    src.write_text(TOO_BIG)
    r = subprocess.run([cxx, "-std=gnu++17", f"-I{INC}", "-fsyntax-only", str(src)], capture_output=True, text=True)
    assert r.returncode != 0
    assert "bigger than two pointers" in r.stderr
