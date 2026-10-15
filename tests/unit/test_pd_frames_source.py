"""The USB PD source-side frame logic (libraries/CH32RVUsbPd/src/pd_frames.c), on the host.

pd_frames.c knows no registers, so the host compiler builds it and ctypes calls it. Expected words
are encoded here independently from the field layouts of USB PD R3.1, and the source's offers and
the sink's requests are run through each other's code, so a layout mistake has to be made twice to
pass.
"""
import ctypes
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
SRC = REPO / "libraries/CH32RVUsbPd/src/pd_frames.c"
PD_PDO_MAX = 7
FIXED, PPS = 0, 1


class Pdo(ctypes.Structure):
    _fields_ = [("kind", ctypes.c_uint8), ("min_mv", ctypes.c_uint16), ("max_mv", ctypes.c_uint16),
                ("max_ma", ctypes.c_uint16), ("max_mw", ctypes.c_uint32), ("raw", ctypes.c_uint32)]


class Caps(ctypes.Structure):
    _fields_ = [("count", ctypes.c_uint8), ("usb_comm", ctypes.c_uint8), ("unconstrained", ctypes.c_uint8),
                ("dual_role_power", ctypes.c_uint8), ("pdo", Pdo * PD_PDO_MAX)]


class Request(ctypes.Structure):
    _fields_ = [("index", ctypes.c_uint8), ("pps", ctypes.c_uint8), ("mv", ctypes.c_uint16),
                ("ma", ctypes.c_uint16), ("mismatch", ctypes.c_uint8)]


@pytest.fixture(scope="module")
def lib(tmp_path_factory):
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no host C compiler")
    out = tmp_path_factory.mktemp("pd_frames") / "pd_frames.so"
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC", str(SRC), "-o", str(out)],
                   check=True)
    lib = ctypes.CDLL(str(out))
    lib.pd_header_source.restype = ctypes.c_uint16
    lib.pd_source_pdo_fixed.restype = ctypes.c_uint32
    lib.pd_source_pdo_fixed.argtypes = [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint32]
    lib.pd_source_pdo_pps.restype = ctypes.c_uint32
    lib.pd_source_pdo_pps.argtypes = [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint16]
    lib.pd_request_fixed.restype = ctypes.c_uint32
    lib.pd_request_fixed.argtypes = [ctypes.c_uint8, ctypes.c_uint16, ctypes.c_uint16]
    lib.pd_request_pps.restype = ctypes.c_uint32
    lib.pd_request_pps.argtypes = [ctypes.c_uint8, ctypes.c_uint16, ctypes.c_uint16]
    lib.pd_check_request.argtypes = [ctypes.POINTER(Caps), ctypes.c_uint32, ctypes.POINTER(Request)]
    return lib


def offer(lib, words):
    caps = Caps()
    arr = (ctypes.c_uint32 * len(words))(*words)
    lib.pd_parse_source_caps(arr, ctypes.c_uint8(len(words)), ctypes.byref(caps))
    return caps


def test_source_header_carries_source_and_dfp_roles(lib):
    h = lib.pd_header_source(1, 2, 5, 2)          # Source_Capabilities, 2 objects, id 5, PD 3.0
    assert h == 1 | (1 << 5) | (2 << 6) | (1 << 8) | (5 << 9) | (2 << 12)


def test_fixed_and_pps_pdos_encode_the_spec_fields(lib):
    assert lib.pd_source_pdo_fixed(5000, 3000, 0) == (100 << 10) | 300
    assert lib.pd_source_pdo_fixed(9000, 2000, 1 << 26) == (1 << 26) | (180 << 10) | 200
    assert lib.pd_source_pdo_pps(3300, 11000, 2000) == (3 << 30) | (110 << 17) | (33 << 8) | 40


def test_the_sink_parser_reads_back_what_the_source_offers(lib):
    caps = offer(lib, [lib.pd_source_pdo_fixed(5000, 3000, 0), lib.pd_source_pdo_fixed(9000, 2000, 0),
                       lib.pd_source_pdo_pps(3300, 11000, 2000)])
    assert caps.count == 3
    assert (caps.pdo[0].kind, caps.pdo[0].max_mv, caps.pdo[0].max_ma) == (FIXED, 5000, 3000)
    assert (caps.pdo[1].kind, caps.pdo[1].max_mv, caps.pdo[1].max_ma) == (FIXED, 9000, 2000)
    assert (caps.pdo[2].kind, caps.pdo[2].min_mv, caps.pdo[2].max_mv, caps.pdo[2].max_ma) == (PPS, 3300, 11000, 2000)


def check(lib, caps, rdo):
    out = Request()
    ok = lib.pd_check_request(ctypes.byref(caps), rdo, ctypes.byref(out))
    return ok, out


def test_the_sinks_requests_are_accepted_inside_the_offer(lib):
    caps = offer(lib, [lib.pd_source_pdo_fixed(5000, 3000, 0), lib.pd_source_pdo_fixed(9000, 2000, 0),
                       lib.pd_source_pdo_pps(3300, 11000, 2000)])
    ok, r = check(lib, caps, lib.pd_request_fixed(2, 1500, 2000))
    assert ok and (r.index, r.pps, r.mv, r.ma) == (1, 0, 9000, 1500)
    ok, r = check(lib, caps, lib.pd_request_pps(3, 5020, 1500))
    assert ok and (r.index, r.pps, r.mv, r.ma) == (2, 1, 5020, 1500)


def test_requests_outside_the_offer_are_rejected(lib):
    caps = offer(lib, [lib.pd_source_pdo_fixed(5000, 3000, 0), lib.pd_source_pdo_pps(3300, 11000, 2000)])
    assert not check(lib, caps, lib.pd_request_fixed(1, 3500, 3500))[0]       # more than the PDO's 3 A
    assert not check(lib, caps, lib.pd_request_fixed(3, 1000, 1000))[0]       # no third PDO
    assert not check(lib, caps, (0 << 28) | (100 << 10) | 100)[0]             # position 0
    assert not check(lib, caps, lib.pd_request_pps(2, 12000, 1000))[0]        # above the PPS range
    assert not check(lib, caps, lib.pd_request_pps(2, 3000, 1000))[0]         # below it
    assert not check(lib, caps, lib.pd_request_pps(2, 5000, 2500))[0]         # more than its 2 A


def test_a_capability_mismatch_may_ask_for_more_than_offered_as_maximum(lib):
    caps = offer(lib, [lib.pd_source_pdo_fixed(5000, 1500, 0)])
    rdo = lib.pd_request_fixed(1, 1500, 3000)
    assert not check(lib, caps, rdo)[0]
    ok, r = check(lib, caps, rdo | (1 << 26))
    assert ok and r.mismatch == 1
