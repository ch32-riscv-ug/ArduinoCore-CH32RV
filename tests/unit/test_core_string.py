"""The core's word-at-a-time memcpy / memmove / memset (cores/arduino/ch32rv_string.c, ch32rv_memmove.c,
ch32rv_memset.c), on the host.

Built with the host compiler under other names (the host's libc keeps its own), called through
ctypes, and compared with what Python's bytes say the result must be, over every length to 70,
every source and destination offset within a word, and memmove's overlaps in both directions.
"""
import ctypes
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
SRCS = [REPO / "cores/arduino" / name for name in ("ch32rv_string.c", "ch32rv_memmove.c", "ch32rv_memset.c")]


@pytest.fixture(scope="module")
def lib(tmp_path_factory):
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no host C compiler")
    out = tmp_path_factory.mktemp("ch32rv_string") / "ch32rv_string.so"
    subprocess.run([cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-shared", "-fPIC",
                    "-Dmemcpy=t_memcpy", "-Dmemmove=t_memmove", "-Dmemset=t_memset", *map(str, SRCS), "-o", str(out)],
                   check=True)
    lib = ctypes.CDLL(str(out))
    for name in ("t_memcpy", "t_memmove"):
        getattr(lib, name).argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
        getattr(lib, name).restype = ctypes.c_void_p
    lib.t_memset.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_size_t]
    lib.t_memset.restype = ctypes.c_void_p
    return lib


def pattern(n, seed):
    return bytes((i * 37 + seed) & 0xFF for i in range(n))


def test_memcpy(lib):
    for n in range(71):
        for so in range(4):
            for do in range(4):
                src = ctypes.create_string_buffer(pattern(n + 8, so), n + 8)
                dst = ctypes.create_string_buffer(b"\xAA" * (n + 8), n + 8)
                ret = lib.t_memcpy(ctypes.addressof(dst) + do, ctypes.addressof(src) + so, n)
                assert ret == ctypes.addressof(dst) + do
                want = b"\xAA" * do + src.raw[so:so + n] + b"\xAA" * (8 - do)
                assert dst.raw == want, (n, so, do)


def test_memset(lib):
    for n in range(71):
        for do in range(4):
            dst = ctypes.create_string_buffer(b"\x11" * (n + 8), n + 8)
            lib.t_memset(ctypes.addressof(dst) + do, 0x1C5, n)      # only the low byte counts
            assert dst.raw == b"\x11" * do + b"\xC5" * n + b"\x11" * (8 - do), (n, do)


def test_memmove_overlaps(lib):
    size = 96
    for n in range(0, 71):
        for src_off in range(0, 12):
            for dst_off in range(0, 12):
                buf = ctypes.create_string_buffer(pattern(size, 3), size)
                before = bytearray(buf.raw)
                lib.t_memmove(ctypes.addressof(buf) + dst_off, ctypes.addressof(buf) + src_off, n)
                want = bytearray(before)
                want[dst_off:dst_off + n] = before[src_off:src_off + n]
                assert buf.raw == bytes(want), (n, src_off, dst_off)
