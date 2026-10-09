"""runtime.init, runtime.time-progress, runtime.print-heap, console.round-trip."""
import json
import pathlib
import secrets
import time


def test_runtime_contract(dut, record_property):
    settings = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    nonce = secrets.token_hex(4)
    dut.write(f"ready {nonce}\n")
    match = dut.expect(rb"READY " + nonce.encode() + rb" (\S+) (\d+)\r?\n", timeout=15)
    assert match.group(1).decode() == settings["build_id"]
    assert int(match.group(2)) >= 1
    dut.write("init\n")
    dut.expect_exact("INIT 13579BDF 0 2468ACE0", timeout=10)
    dut.write("print\n")
    dut.expect_exact("PRINT ch321234 1234ABCD -42 1101", timeout=10)
    dut.write("heap\n")
    dut.expect_exact("HEAP 496", timeout=10)
    started = time.monotonic()
    dut.write("time\n")
    m = dut.expect(rb"TIME (\d+) (\d+) (\d+)\r?\n", timeout=10)
    host_elapsed = time.monotonic() - started
    elapsed_ms, elapsed_us, f_cpu = map(int, m.groups())
    assert 45 <= elapsed_ms <= 100
    assert 800 <= elapsed_us <= 2500
    assert f_cpu == settings["f_cpu"]
    assert 0.035 <= host_elapsed < 10
    record_property("elapsed_ms", elapsed_ms)
    record_property("elapsed_us", elapsed_us)
    record_property("host_elapsed_s", host_elapsed)
    # A second challenge proves that loop and the monitor still work after the checks.
    nonce2 = secrets.token_hex(4)
    dut.write(f"ready {nonce2}\n")
    dut.expect_exact(f"READY {nonce2} {settings['build_id']}", timeout=10)
