"""flash.units / flash.kept / flash.time: ch32rv_flash.h on the DUT itself
(tests/sketch_support/flash/flash.ino). No wiring. About 16 erases of two stores
at the end of flash per run.

- flash.units: every function (erase, write, erase_page, write_page,
  rewrite_page) on a scratch store, a two-unit erase, a write from unaligned
  data, and the refusals (the program itself, unaligned, past FLASH, empty).
- flash.kept: a value saved with rewrite_page() is read back by setup() after a
  software reset.
- flash.time: one rewrite_page() takes a sane time; whether millis() kept up is
  recorded (it falls behind where the CPU stops while the flash is busy: V003,
  V00x, M030, V103, L103, X035, V205)."""
import json
import pathlib
import secrets
import time

import pexpect


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def test_flash_contract(dut, record_property):
    build_id = settings(__file__)["build_id"]
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+) (\d+) (\d+) ([0-9A-F]+)", timeout=15)
    assert m.group(1).decode() == build_id, "the DUT runs another build"
    erase_size, write_size, erased = int(m.group(2)), int(m.group(3)), int(m.group(4), 16)
    record_property("erase_size", erase_size)
    record_property("write_size", write_size)
    assert erase_size in (64, 128, 256, 4096, 8192) and write_size in (64, 128, 256)
    assert erased in (0xFFFFFFFF, 0xE339E339)

    # flash.units
    m = ask(dut, "units", rb"UNITS (\d+) ([0-9A-F]+)", timeout=30)
    assert int(m.group(1)) == 16
    assert int(m.group(2), 16) == 0, f"failed steps (bitmap, see flash.ino): {m.group(2).decode()}"

    # flash.kept
    value = secrets.randbelow(0xFFFFFFF0) + 1
    assert ask(dut, f"save {value}", rb"SAVE (\d)").group(1) == b"1"
    dut.write("restart\n")
    # Lines sent while the DUT restarts are lost; ask again until the new run answers.
    for attempt in range(6):
        time.sleep(1)
        nonce = secrets.token_hex(4)
        try:
            ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" \S+ \d+ \d+ [0-9A-F]+", timeout=5)
            break
        except pexpect.TIMEOUT:
            assert attempt < 5, "no answer after the software reset"
    m = ask(dut, "boot", rb"BOOT (\d) (\d+)")
    assert m.group(1) == b"1" and int(m.group(2)) == value, "the saved value did not survive the reset"

    # flash.time
    m = ask(dut, "time", rb"TIME (\d+) (\d+) (\d+)", timeout=15)
    us, ms, micros = map(int, m.groups())
    record_property("rewrite_us", us)
    record_property("millis_advance", ms)
    record_property("micros_advance", micros)
    assert 300 <= us <= 60000, "one rewrite_page() took an unlikely time"
