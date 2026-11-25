"""usb-ss-iso: SuperSpeed isochronous intervals of more than one burst (the endpoint companion's Mult > 0).

The DUT runs tests/sketch_support/usb_ss_iso: a vendor interface whose alternate settings have isochronous
endpoints of 1024 bytes at bInterval 1 (125 us) and companions of bMaxBurst / Mult 1/0 (the one-burst baseline),
1/1, 1/2 and 15/2 (48 packets, IN and OUT apart). The controller has no Mult setting (RM 27: no such field); the
driver sends and takes a whole service interval as one chain, so the question is whether the host gets (and the DUT
gets) every burst of an interval. This host reads what an interval holds from the descriptors (the companion), asks
the DUT what it serves, and through libusb (usb_iso.py: one libusb packet is one service interval at SuperSpeed)
looks at each interval's bytes:

  whole    every packet of the interval, in order, each carrying the interval's number and its own
  empty    nothing: the DUT had no transfer armed (its USB task was late) or the host saw an error
  partial  some of the packets: the signature of Mult not served (one burst of the interval, 1 / (Mult + 1))

A controller that served one burst an interval would make every interval of alternate settings 2-5 partial.
Passes when partial intervals are at most 1 % and whole ones at least 90 %; see the bounds below.

Beside it, test_usb_vendor_halt: an endpoint halt and its clearing on a vendor bulk interface of the same sketch,
whichever side halts it (the PC's libusb is the only host here that sends CLEAR_FEATURE on its own).
"""
import json
import pathlib
import secrets
import struct
import sys
import time

import pytest
import usb.core
import usb.util

# The runner stages the helpers beside this file; pytest imports tests without adding their directory.
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from usb_host import APP, PcUsbHost  # noqa: E402
from usb_iso import iso_transfer  # noqa: E402
import bench_ready   # staged beside the test (run_hardware.py)

# Bounds, from what decides each:
# - PARTIAL_MAX: a partial interval comes from the controller (a burst not served), or from the DUT arming its
#   chain in the middle of an interval (the host's first burst found none; then the chain starts in a later burst).
#   The DUT arms the next interval as soon as the last completes, from its USB task, with nothing else in its loop,
#   so that race is rare; a Mult the controller does not serve makes every interval partial. 1 % separates the two
#   by two orders of magnitude.
#   Not counted as partial: the rest of a chain after an interval the host saw as an error (its first burst there), a
#   link error showing twice, not Mult (iso_in_spilled).
# - WHOLE_MIN: empty intervals are the DUT's task timing and link errors (the usb-media microphone at SuperSpeed
#   loses about 0.1 % to the latter), not what this case asks; 90 % still needs the driver to keep up interval
#   after interval.
PARTIAL_MAX = 0.01
WHOLE_MIN = 0.90
# usbfs buffers a transfer's data in kernel memory (usbfs_memory_mb, 16 MiB by default, for the whole process with
# its packet descriptors): 8 MiB in one transfer of the 48-packet alternate settings (170 intervals of 48 KiB) was
# refused with ENOMEM (2026-11-17), so at most 2 MiB per transfer (42 intervals there), and at most 800 intervals
# (100 ms).
TRANSFER_BYTES_MAX = 2 << 20
INTERVALS_MAX = 800
# Alternate settings at most one SuperSpeed port may not have the bandwidth for (393 MB/s periodic): the host
# refusing SET_INTERFACE there is the host's limit, reported as a skip.
LARGE = (4, 5)


def _companion(ep):
    """(bMaxBurst, Mult, wBytesPerInterval) of a SuperSpeed endpoint, from the descriptor after it."""
    extra = bytes(ep.extra_descriptors)
    i = 0
    while i + 1 < len(extra) and extra[i]:
        if extra[i + 1] == 0x30 and extra[i] >= 6:
            return extra[i + 2], extra[i + 3] & 3, extra[i + 4] | extra[i + 5] << 8
        i += extra[i]
    raise AssertionError(f"endpoint {ep.bEndpointAddress:#04x} has no SuperSpeed companion")


def _ready(dut):
    settings = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    build_id = settings["build_id"]
    host = PcUsbHost.by_serial(settings["target"]["dut_usb_serial"])
    nonce = secrets.token_hex(4)
    m = bench_ready.ready(dut, rb" (\S+) (\d) (\S+)", nonce=nonce)
    assert m.group(1).decode() == build_id
    assert m.group(2) == b"1", "USBDevice.begin() failed: " + m.group(3).decode()
    assert host.wait_ids(APP, 10), "application device did not enumerate"
    deadline = time.monotonic() + 5
    while not host.product().endswith(build_id) and time.monotonic() < deadline:
        time.sleep(0.05)
    assert host.product().endswith(build_id), "the device is not this build: " + host.product()
    return host


def _dut_iso(dut):
    dut.write("iso")
    m = dut.expect(rb"ISO" + rb" (\d+)" * 12 + rb"\r?\n", timeout=5)
    keys = ("alt", "packets", "in_buffers", "in_submitted", "in_refused", "out_intervals", "out_whole", "out_short",
            "out_bad", "in_gap_max_us", "in_gaps_long", "in_resubmit_max_us")
    return dict(zip(keys, (int(g) for g in m.groups())))


def _stream(dut, record_property, alt, direction):
    """Select `alt`, run one transfer of intervals `direction` ("in" / "out"), return what to check."""
    host = _ready(dut)
    dev = host.device()
    if dev.speed != usb.util.SPEED_SUPER:
        pytest.skip(f"n/a: the DUT's link is not SuperSpeed (speed {dev.speed}); Mult is SuperSpeed's")
    cfg = dev.get_active_configuration()
    number = next(i.bInterfaceNumber for i in cfg if (i.bInterfaceClass, i.bInterfaceSubClass) == (0xFF, 0x49))
    usb.util.dispose_resources(dev)
    with host.claimed([number]) as dev:
        cfg = dev.get_active_configuration()
        itf = cfg[(number, alt)]
        ep = next(e for e in itf if bool(e.bEndpointAddress & 0x80) == (direction == "in"))
        burst, mult, per_interval = _companion(ep)
        packets = (burst + 1) * (mult + 1)
        mps = ep.wMaxPacketSize & 0x7FF
        record_property("iso_alt", alt)
        record_property("iso_companion", f"bMaxBurst {burst} Mult {mult} wBytesPerInterval {per_interval}")
        assert per_interval == packets * mps, \
            f"wBytesPerInterval {per_interval} is not (bMaxBurst + 1) x (Mult + 1) x {mps}"
        try:
            dev.set_interface_altsetting(interface=number, alternate_setting=alt)
        except usb.core.USBError as e:
            if alt in LARGE:
                pytest.skip(f"skip: the host refused alternate setting {alt} ({per_interval} bytes every 125 us "
                            f"each way): {e}")
            raise
        time.sleep(0.05)
        served = _dut_iso(dut)
        assert served["alt"] == alt and served["packets"] == packets, f"the DUT serves {served}"
        if direction == "in":
            assert served["in_buffers"] >= 1, "the DUT could not allocate an interval's buffer"
        intervals = min(INTERVALS_MAX, TRANSFER_BYTES_MAX // per_interval)
        statuses = []
        if direction == "in":
            got = iso_transfer(dev, ep.bEndpointAddress, per_interval, intervals, statuses=statuses)
            sent = None
        else:
            data = bytearray(per_interval * intervals)
            for s in range(intervals):
                for k in range(packets):
                    at = (s * packets + k) * mps
                    struct.pack_into("<IH", data, at, s, k)
                    data[at + mps - 1] = (s + k) & 0xFF
            sent = iso_transfer(dev, ep.bEndpointAddress, per_interval, intervals, data=data, statuses=statuses)
            got = None
        time.sleep(0.05)
        served = _dut_iso(dut)
        dev.set_interface_altsetting(interface=number, alternate_setting=0)
    record_property("iso_dut", json.dumps(served))
    return packets, mps, intervals, got, sent, statuses, served


@pytest.mark.parametrize("alt", [1, 2, 3, 4])
def test_usb_ss_iso_in(dut, record_property, alt):
    packets, mps, intervals, got, _, statuses, served = _stream(dut, record_property, alt, "in")
    whole = empty = partial = errors = 0
    sizes, last, spills = {}, -1, []
    spilled = 0
    disorder = []
    for i, (p, st) in enumerate(zip(got, statuses)):
        sizes[len(p)] = sizes.get(len(p), 0) + 1
        if st != 0:
            errors += 1
            continue
        if not p:
            empty += 1
            continue
        if len(p) != packets * mps:
            # The rest of a chain whose first burst came in an interval the host saw as an error: the DUT cannot know
            # of that error and goes on with the chain (2026-11-20: every such partial starts past packet 0 and follows
            # an error of the same number). The error is counted there; this is not a Mult left unserved.
            first = struct.unpack_from("<IH", p) if len(p) >= 6 else (None, 0)
            if i and statuses[i - 1] != 0 and first[1] > 0:
                spilled += 1
                continue
            partial += 1
            if len(spills) < 8:     # what a partial interval and the next one carry: (serial, packet) of each packet
                heads = lambda q: [struct.unpack_from("<IH", q, k * mps) for k in range(len(q) // mps)]
                nxt = got[i + 1] if i + 1 < len(got) else b""
                prev = got[i - 1] if i else b""
                spills.append(dict(at=i, prev=heads(prev)[:3] + heads(prev)[-2:], partial=heads(p)[:4],
                                   next=heads(nxt)[:3], statuses=statuses[max(0, i - 1):i + 2]))
            continue
        serial = struct.unpack_from("<I", p)[0]
        for k in range(packets):
            s, kk = struct.unpack_from("<IH", p, k * mps)
            if (s, kk) != (serial, k) or p[k * mps + mps - 1] != (serial + k) & 0xFF:
                disorder.append(f"interval {i} packet {k}: ({s}, {kk}, {p[k * mps + mps - 1]})")
                break
        else:
            if serial <= last:
                disorder.append(f"interval {i}: number {serial} after {last}")
            last = serial
            whole += 1
    record_property("iso_in_intervals", intervals)
    record_property("iso_in_whole", whole)
    record_property("iso_in_empty", empty)
    record_property("iso_in_partial", partial)
    record_property("iso_in_spilled", spilled)
    record_property("iso_in_errors", errors)
    record_property("iso_in_sizes", json.dumps(sizes))
    record_property("iso_in_partial_heads", json.dumps(spills))
    assert not disorder, "packets out of place: " + "; ".join(disorder[:5])
    assert partial <= intervals * PARTIAL_MAX, \
        f"{partial} of {intervals} intervals came with part of their {packets} packets (sizes {sizes}): " \
        f"the bursts after the first are not served"
    assert whole >= intervals * WHOLE_MIN, \
        f"only {whole} of {intervals} intervals whole (empty {empty}, errors {errors}, DUT {served})"


@pytest.mark.parametrize("alt", [1, 2, 3, 5])
def test_usb_ss_iso_out(dut, record_property, alt):
    packets, mps, intervals, _, sent, statuses, served = _stream(dut, record_property, alt, "out")
    errors = sum(1 for st in statuses if st != 0)
    record_property("iso_out_intervals", intervals)
    record_property("iso_out_errors", errors)
    assert sum(sent) == intervals * packets * mps, f"the host sent {sum(sent)} bytes of {intervals * packets * mps}"
    assert served["out_bad"] == 0, f"{served['out_bad']} intervals arrived with a packet out of place"
    assert served["out_intervals"] <= intervals, f"the DUT counted {served['out_intervals']} of {intervals} intervals"
    assert served["out_short"] <= intervals * PARTIAL_MAX, \
        f"{served['out_short']} of {intervals} intervals arrived short of their {packets} packets: the bursts " \
        f"after the first are not taken"
    assert served["out_whole"] >= intervals * WHOLE_MIN, \
        f"only {served['out_whole']} of {intervals} intervals arrived whole (DUT {served}, host errors {errors})"


def _bulk(dev):
    """The vendor bulk interface (0xFF / 0x56 / 0x42): (its number, bulk OUT, bulk IN)."""
    cfg = dev.get_active_configuration()
    itf = next(i for i in cfg if (i.bInterfaceClass, i.bInterfaceSubClass, i.bInterfaceProtocol) == (0xFF, 0x56, 0x42))
    out = next(e for e in itf if not e.bEndpointAddress & 0x80)
    inn = next(e for e in itf if e.bEndpointAddress & 0x80)
    return itf.bInterfaceNumber, out.bEndpointAddress, inn.bEndpointAddress


def _echo(dev, out, inn, n=100):
    data = secrets.token_bytes(n)
    dev.write(out, data, timeout=1000)
    got = bytearray()
    deadline = time.monotonic() + 2
    while len(got) < n and time.monotonic() < deadline:
        try:
            got += bytes(dev.read(inn, 1024, timeout=200))
        except usb.core.USBTimeoutError:
            pass
    return bytes(got) == data, len(got)


def test_usb_vendor_halt(dut, record_property):
    """An endpoint halt both ways, through the host's libusb: the DUT stalls its bulk IN (then OUT) when asked (a
    vendor request, CH32RVUsbDevice::stall()); the host's transfer on it fails with a pipe error (EPIPE), GET_STATUS
    and the DUT's stalled() say halted; CLEAR_FEATURE(ENDPOINT_HALT) (libusb's clear_halt) clears both, and the echo
    of 100 random bytes works again (the sequence numbers start over on both sides). Then the host halts each
    endpoint itself (SET_FEATURE(ENDPOINT_HALT)), the DUT's stalled() says so, and clearing it brings the echo back."""
    host = _ready(dut)
    dev = host.device()
    record_property("speed", dev.speed)
    number, out, inn = _bulk(dev)
    usb.util.claim_interface(dev, number)
    wrong, seen = [], {}

    def halted(ep):
        status = dev.ctrl_transfer(0x82, 0x00, 0, ep, 2, timeout=1000)        # GET_STATUS of the endpoint
        return dev.ctrl_transfer(0xC1, 0x52, ep, number, 1, timeout=1000)[0], status[0] & 1

    try:
        ok, n = _echo(dev, out, inn)
        seen["echo before"] = n
        if not ok:
            wrong.append(f"the echo before any halt: {n} of 100 bytes back")
        for ep, name in ((inn, "IN"), (out, "OUT")):
            dev.ctrl_transfer(0x41, 0x51, ep, number, None, timeout=1000)
            seen[f"{name} stalled by the DUT"] = halted(ep)
            if halted(ep) != (1, 1):
                wrong.append(f"{name} after stall(): stalled() / GET_STATUS {halted(ep)}, want (1, 1)")
            try:
                if ep & 0x80:
                    dev.read(ep, 64, timeout=500)
                else:
                    dev.write(ep, b"x", timeout=500)
                wrong.append(f"{name} stalled: the transfer went through")
            except usb.core.USBError as e:
                seen[f"{name} transfer while stalled"] = e.errno
                if e.errno != 32:
                    wrong.append(f"{name} stalled: the transfer failed with {e!r}, not EPIPE")
            dev.clear_halt(ep)
            seen[f"{name} cleared"] = halted(ep)
            if halted(ep) != (0, 0):
                wrong.append(f"{name} after CLEAR_FEATURE: stalled() / GET_STATUS {halted(ep)}, want (0, 0)")
            ok, n = _echo(dev, out, inn)
            seen[f"echo after {name}"] = n
            if not ok:
                wrong.append(f"the echo after {name}'s halt was cleared: {n} of 100 bytes back")
        for ep, name in ((inn, "IN"), (out, "OUT")):
            dev.ctrl_transfer(0x02, 0x03, 0, ep, None, timeout=1000)        # SET_FEATURE(ENDPOINT_HALT)
            seen[f"{name} halted by the host"] = halted(ep)
            if halted(ep) != (1, 1):
                wrong.append(f"{name} after SET_FEATURE: stalled() / GET_STATUS {halted(ep)}, want (1, 1)")
            dev.clear_halt(ep)
            ok, n = _echo(dev, out, inn)
            seen[f"echo after the host's {name} halt"] = n
            if not ok:
                wrong.append(f"the echo after the host's {name} halt was cleared: {n} of 100 bytes back")
    finally:
        usb.util.release_interface(dev, number)
        usb.util.dispose_resources(dev)
    record_property("halt", json.dumps(seen, default=str))
    assert not wrong, " | ".join(wrong)


def test_usb_vendor_after_reset(dut, record_property):
    """The device after the host resets its port (libusb's reset(): a bus reset and the enumeration again), three
    times: it comes back at the same speed, as this build, and the vendor bulk echo of 100 random bytes works each
    time (the stack's endpoints opened anew, nothing left of the transfers before)."""
    host = _ready(dut)
    seen, wrong = [], []
    for round_ in range(3):
        dev = host.device()
        speed = dev.speed
        number, out, inn = _bulk(dev)
        usb.util.claim_interface(dev, number)
        try:
            ok, n = _echo(dev, out, inn)
        finally:
            usb.util.release_interface(dev, number)
        seen.append(dict(round=round_, speed=speed, echo=n))
        if not ok:
            wrong.append(f"round {round_}: {n} of 100 bytes back")
        try:
            dev.reset()
        except usb.core.USBError as e:              # libusb may report the device gone while it re-enumerates
            seen[-1]["reset"] = repr(e)
        usb.util.dispose_resources(dev)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                if host.device() is not None and host.product().endswith(
                        json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["build_id"]):
                    break
            except (usb.core.USBError, AssertionError, OSError):
                pass
            time.sleep(0.1)
        else:
            wrong.append(f"round {round_}: the device did not come back after the reset")
            break
        if host.device().speed != speed:
            wrong.append(f"round {round_}: back at speed {host.device().speed}, was {speed}")
    record_property("rounds", json.dumps(seen))
    assert not wrong, " | ".join(wrong)


BULK_SIZES = (1, 1023, 1024, 1025, 4096, 16384, 65536)


def test_usb_vendor_bulk_sizes(dut, record_property):
    """The vendor bulk echo at sizes around the packet (1024 at SuperSpeed) and in bursts (16 and 64 KiB: the
    endpoint's bMaxBurst lets the host send several packets before an ACK), the host writing from one thread while
    it reads in another (the DUT's FIFO is smaller than the larger sizes); and a zero-length packet from the host,
    which the DUT takes as an empty transfer with no echo, the next echo intact. Every byte back, in order."""
    import threading
    host = _ready(dut)
    dev = host.device()
    number, out, inn = _bulk(dev)
    usb.util.claim_interface(dev, number)
    seen, wrong = {}, []
    try:
        for size in BULK_SIZES:
            data = secrets.token_bytes(size)
            errors = []

            def writer():
                try:
                    dev.write(out, data, timeout=5000)
                except usb.core.USBError as e:
                    errors.append(repr(e))

            t = threading.Thread(target=writer)
            started = time.monotonic()
            t.start()
            got = bytearray()
            deadline = time.monotonic() + 10
            while len(got) < size and time.monotonic() < deadline:
                try:
                    got += bytes(dev.read(inn, 16384, timeout=200))
                except usb.core.USBTimeoutError:
                    pass
            t.join(10)
            took = time.monotonic() - started
            seen[size] = dict(back=len(got), ms=round(took * 1000, 1), errors=errors)
            if bytes(got) != data or errors:
                wrong.append(f"{size} bytes: {len(got)} back{' (differing)' if len(got) == size else ''}"
                             + (f", the write: {errors[0]}" if errors else ""))
        dev.write(out, b"", timeout=1000)
        ok, n = _echo(dev, out, inn)
        seen["after a ZLP"] = n
        if not ok:
            wrong.append(f"the echo after a zero-length packet: {n} of 100 bytes back")
    finally:
        usb.util.release_interface(dev, number)
        usb.util.dispose_resources(dev)
    record_property("bulk", json.dumps(seen))
    assert not wrong, " | ".join(wrong)


RESTARTS = 10


def test_usb_ss_enumeration_after_restart(dut, record_property):
    """The DUT restarts itself (CH32RV.restart()) ten times; each time the host enumerates it again at SuperSpeed
    (the bench once saw 1 in 4-5 resets fall back to full speed, the kernel reporting -71 on the descriptors,
    2026-11-11) and the vendor bulk echo works. Records each round's speed."""
    settings = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    host = _ready(dut)
    speeds, wrong = [], []
    for round_ in range(RESTARTS):
        before = host.devnum()
        dut.write("restart")
        dut.expect(rb"STARTUP", timeout=15)
        if not host.wait_reenumerated(before, 10):
            wrong.append(f"round {round_}: the host did not enumerate the DUT again at {host.topology} "
                         f"(device number {before} -> {host.devnum()})")
            break
        host = _ready(dut)
        dev = host.device()
        speeds.append(dev.speed)
        if dev.speed != usb.util.SPEED_SUPER:
            wrong.append(f"round {round_}: enumerated at speed {dev.speed}, not SuperSpeed")
            continue
        number, out, inn = _bulk(dev)
        usb.util.claim_interface(dev, number)
        try:
            ok, n = _echo(dev, out, inn)
        finally:
            usb.util.release_interface(dev, number)
            usb.util.dispose_resources(dev)
        if not ok:
            wrong.append(f"round {round_}: the echo gave {n} of 100 bytes back")
    record_property("speeds", json.dumps(speeds))
    assert not wrong, " | ".join(wrong)


UNPLUGS = 3


def test_usb_vendor_after_unplug(dut, record_property):
    """The cable pulled and put back, three times: the hub port the DUT hangs from (its parent in the topology) has its
    power cut for a second and given back (uhubctl -a cycle; the hub must switch its ports one by one, ppps). The DUT,
    powered from the probe, sees VBUS go: it comes back at the same speed as this build, and the vendor bulk echo of
    100 random bytes works each time. Skips where the parent is not such a hub or uhubctl cannot switch it."""
    import shutil
    import subprocess
    settings = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    host = _ready(dut)
    hub, _, port = host.topology.rpartition(".")
    if not hub or "-" not in hub or not shutil.which("uhubctl"):
        pytest.skip("the DUT is not behind a hub port uhubctl can reach (topology " + host.topology + ")")
    status = subprocess.run(["uhubctl", "-l", hub], capture_output=True, text=True, timeout=30)
    if status.returncode or "ppps" not in status.stdout:
        pytest.skip(f"hub {hub} does not switch its ports one by one (uhubctl: {status.stdout.strip()[:120]})")
    speed = host.device().speed
    rounds, wrong = [], []
    for round_ in range(UNPLUGS):
        t0 = time.monotonic()
        cycle = subprocess.run(["uhubctl", "-l", hub, "-p", port, "-a", "cycle", "-d", "1"], capture_output=True,
                               text=True, timeout=30)
        if cycle.returncode:
            pytest.skip(f"uhubctl could not switch {hub} port {port}: {cycle.stderr.strip()[:120]}")
        back = False
        while time.monotonic() - t0 < 15:
            try:
                dev = host.device()
                if host.product().endswith(settings["build_id"]):
                    back = True
                    break
            except (AssertionError, usb.core.USBError, OSError):
                pass
            time.sleep(0.1)
        if not back:
            wrong.append(f"round {round_}: not back 15 s after the power cycle")
            break
        entry = dict(round=round_, back_s=round(time.monotonic() - t0, 2), speed=dev.speed)
        if dev.speed != speed:
            wrong.append(f"round {round_}: back at speed {dev.speed}, was {speed}")
        number, out, inn = _bulk(dev)
        usb.util.claim_interface(dev, number)
        try:
            ok, n = _echo(dev, out, inn)
        finally:
            usb.util.release_interface(dev, number)
            usb.util.dispose_resources(dev)
        entry["echo"] = n
        rounds.append(entry)
        if not ok:
            wrong.append(f"round {round_}: the echo gave {n} of 100 bytes back")
    record_property("unplugs", json.dumps(rounds))
    assert not wrong, " | ".join(wrong)
