"""The USB PD frame logic in libraries/USBPD, exercised on the host.

The library splits protocol logic from hardware exactly so this test can
exist: pd_frames.c knows no registers, so the host compiler can build it as a
shared object and ctypes can call it. The same vectors run on the target in
tests/sketches/basic/pd_selftest, which is what proves the shifts on rv32ec;
this file is where the coverage is, because adding a case here is free.

The expected words are ENCODED HERE, INDEPENDENTLY, from the spec's field
layouts (USB PD R3.1). The C decodes what Python encoded, so a layout error
has to be made twice, in two languages, to slip through - the same
cross-check idea as the startup-equivalence tests.
"""
import ctypes
import pathlib
import subprocess

import pytest

from loader import REPO

SRC = REPO / "libraries" / "USBPD" / "src" / "pd_frames.c"

PD_PDO_MAX = 7
FIXED, PPS, BATTERY, VARIABLE, UNKNOWN = range(5)


class Pdo(ctypes.Structure):
    _fields_ = [("kind", ctypes.c_uint8),
                ("min_mv", ctypes.c_uint16),
                ("max_mv", ctypes.c_uint16),
                ("max_ma", ctypes.c_uint16),
                ("max_mw", ctypes.c_uint32),
                ("raw", ctypes.c_uint32)]


class Caps(ctypes.Structure):
    _fields_ = [("count", ctypes.c_uint8),
                ("usb_comm", ctypes.c_uint8),
                ("unconstrained", ctypes.c_uint8),
                ("dual_role_power", ctypes.c_uint8),
                ("pdo", Pdo * PD_PDO_MAX)]


@pytest.fixture(scope="module")
def lib(tmp_path_factory):
    out = tmp_path_factory.mktemp("pd_frames") / "pd_frames.so"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-shared", "-fPIC", str(SRC), "-o", str(out)], check=True)
    lib = ctypes.CDLL(str(out))
    lib.pd_header.restype = ctypes.c_uint16
    lib.pd_pick.restype = ctypes.c_int
    for f in ("pd_request_fixed", "pd_request_pps", "pd_request_for"):
        getattr(lib, f).restype = ctypes.c_uint32
    lib.pd_supply_name.restype = ctypes.c_char_p
    return lib


def parse(lib, words):
    caps = Caps()
    arr = (ctypes.c_uint32 * len(words))(*words)
    lib.pd_parse_source_caps(arr, len(words), ctypes.byref(caps))
    return caps


# --- independent encoders, straight from the spec's bit numbers --------------

def fixed(mv, ma, flags=0):
    return flags | ((mv // 50) << 10) | (ma // 10)


def pps(min_mv, max_mv, ma):
    return (0b11 << 30) | ((max_mv // 100) << 17) | ((min_mv // 100) << 8) \
           | (ma // 50)


def battery(min_mv, max_mv, mw):
    return (0b01 << 30) | ((max_mv // 50) << 20) | ((min_mv // 50) << 10) \
           | (mw // 250)


def variable(min_mv, max_mv, ma):
    return (0b10 << 30) | ((max_mv // 50) << 20) | ((min_mv // 50) << 10) \
           | (ma // 10)


# A typical 65 W PPS charger: five fixed levels and two adjustable ranges.
# Flags on the first PDO: DRP (B29), unconstrained (B27), USB comm (B26).
CHARGER = [
    fixed(5000, 3000, flags=(1 << 29) | (1 << 27) | (1 << 26)),
    fixed(9000, 3000),
    fixed(12000, 3000),
    fixed(15000, 3000),
    fixed(20000, 3250),
    pps(3300, 11000, 5000),
    pps(3300, 21000, 5000),
]


def test_the_charger_parses(lib):
    caps = parse(lib, CHARGER)
    assert caps.count == 7
    assert [p.kind for p in caps.pdo] == [FIXED] * 5 + [PPS] * 2
    assert (caps.pdo[1].min_mv, caps.pdo[1].max_mv, caps.pdo[1].max_ma) \
        == (9000, 9000, 3000)
    assert (caps.pdo[4].max_mv, caps.pdo[4].max_ma) == (20000, 3250)
    assert (caps.pdo[5].min_mv, caps.pdo[5].max_mv, caps.pdo[5].max_ma) \
        == (3300, 11000, 5000)
    assert (caps.pdo[6].min_mv, caps.pdo[6].max_mv) == (3300, 21000)


def test_the_first_pdo_carries_the_flags(lib):
    caps = parse(lib, CHARGER)
    assert (caps.dual_role_power, caps.unconstrained, caps.usb_comm) \
        == (1, 1, 1)
    # And only the first: a source whose PDO 1 is not fixed gets no flags,
    # rather than flags read out of some other PDO's voltage bits.
    caps = parse(lib, [pps(3300, 11000, 5000)])
    assert (caps.dual_role_power, caps.unconstrained, caps.usb_comm) \
        == (0, 0, 0)


def test_battery_and_variable_parse_but_are_never_picked(lib):
    caps = parse(lib, [fixed(5000, 3000),
                       battery(4750, 21000, 45000),
                       variable(5000, 21000, 3000)])
    assert caps.pdo[1].kind == BATTERY
    assert (caps.pdo[1].min_mv, caps.pdo[1].max_mv) == (4750, 21000)
    assert caps.pdo[1].max_mw == 45000
    assert caps.pdo[1].max_ma == 0
    assert caps.pdo[2].kind == VARIABLE
    assert caps.pdo[2].max_ma == 3000
    # 10 V sits inside both ranges; neither is requestable.
    assert lib.pd_pick(ctypes.byref(caps), 10000, 0) == -1
    assert lib.pd_request_for(ctypes.byref(caps), 1, 10000, 0) == 0


def test_an_avs_apdo_is_unknown_not_garbage(lib):
    # APDO subtype 01 (EPR AVS): B28 set. Listed as unknown, never decoded
    # with the PPS field layout, never requested.
    caps = parse(lib, [fixed(5000, 3000), (0b11 << 30) | (1 << 28) | 0x123456])
    assert caps.pdo[1].kind == UNKNOWN
    assert (caps.pdo[1].min_mv, caps.pdo[1].max_mv, caps.pdo[1].max_ma) \
        == (0, 0, 0)
    assert lib.pd_request_for(ctypes.byref(caps), 1, 5000, 0) == 0


def test_pick_prefers_fixed_at_the_exact_voltage(lib):
    caps = parse(lib, CHARGER)
    # 9 V is inside both PPS ranges, but the fixed 9 V needs no keepalive.
    assert lib.pd_pick(ctypes.byref(caps), 9000, 0) == 1
    assert lib.pd_pick(ctypes.byref(caps), 5000, 0) == 0


def test_pick_uses_pps_between_fixed_levels(lib):
    caps = parse(lib, CHARGER)
    # 5.9 V exists on no fixed level. Both PPS ranges hold it and offer the
    # same current, so the first wins.
    assert lib.pd_pick(ctypes.byref(caps), 5900, 0) == 5


def test_pick_moves_to_pps_when_fixed_current_is_short(lib):
    caps = parse(lib, CHARGER)
    # 9 V at 4 A: the fixed 9 V tops out at 3 A, the PPS ranges carry 5 A.
    assert lib.pd_pick(ctypes.byref(caps), 9000, 4000) == 5
    # 20 V at 4 A: fixed 20 V has 3.25 A; only the 21 V PPS range reaches.
    assert lib.pd_pick(ctypes.byref(caps), 20000, 4000) == 6


def test_pick_refuses_rather_than_rounding(lib):
    caps = parse(lib, [fixed(5000, 3000), fixed(9000, 3000)])
    assert lib.pd_pick(ctypes.byref(caps), 8000, 0) == -1
    caps = parse(lib, CHARGER)
    assert lib.pd_pick(ctypes.byref(caps), 40000, 0) == -1
    assert lib.pd_pick(ctypes.byref(caps), 9000, 9000) == -1


def test_fixed_request_word(lib):
    # Position 2 (the 9 V profile), 3 A operating and maximum: the spec's
    # B31..28 | B19..10 | B9..0 layout, encoded here independently.
    want = (2 << 28) | ((3000 // 10) << 10) | (3000 // 10)
    caps = parse(lib, CHARGER)
    assert lib.pd_request_for(ctypes.byref(caps), 1, 9000, 0) == want
    assert lib.pd_request_fixed(2, 3000, 3000) == want


def test_pps_request_word_truncates_to_20mv(lib):
    # Position 6, 5.9 V at 2 A -> voltage field 295, current field 40.
    want = (6 << 28) | ((5900 // 20) << 9) | (2000 // 50)
    caps = parse(lib, CHARGER)
    assert lib.pd_request_for(ctypes.byref(caps), 5, 5900, 2000) == want
    # 5905 mV is not a 20 mV step; it truncates down, never up.
    assert lib.pd_request_pps(6, 5905, 2000) == want


def test_request_never_exceeds_the_profile(lib):
    caps = parse(lib, CHARGER)
    # Asking 9 V "at whatever you have" requests the profile's own 3 A cap,
    # not a made-up number the source would Reject.
    word = lib.pd_request_for(ctypes.byref(caps), 1, 9000, 0)
    assert word & 0x3FF == 300
    # A PPS voltage outside the named profile's range is a refusal.
    assert lib.pd_request_for(ctypes.byref(caps), 5, 12000, 0) == 0
    # And so are indexes that name nothing.
    assert lib.pd_request_for(ctypes.byref(caps), -1, 9000, 0) == 0
    assert lib.pd_request_for(ctypes.byref(caps), 7, 9000, 0) == 0


def test_header_roundtrip_and_fields(lib):
    # Request (type 2), one object, id 3, PD 3.0.
    h = lib.pd_header(2, 1, 3, 2)
    assert h == (1 << 12) | (3 << 9) | (2 << 6) | 2
    assert lib.pd_header_type(h) == 2
    assert lib.pd_header_count(h) == 1
    assert lib.pd_header_id(h) == 3
    assert lib.pd_header_rev(h) == 2
    assert lib.pd_header_extended(h) == 0
    assert lib.pd_header_extended(0x8000) == 1


def test_a_count_beyond_the_wire_format_is_clamped(lib):
    caps = parse(lib, [fixed(5000, 3000)] * 9)
    assert caps.count == PD_PDO_MAX


def test_supply_names(lib):
    assert lib.pd_supply_name(FIXED) == b"Fixed"
    assert lib.pd_supply_name(PPS) == b"PPS"
    assert lib.pd_supply_name(99) == b"?"
