"""Run the actual UART example parsers with chunked input and a controlled clock.

The stand-in supplies only byte buffering, Print output and millis(); this does
not verify the UART driver, interrupt timing or hardware buffer capacity.
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
PRELUDE = r"""
#include <cstdint>
#include <cstddef>
#include <deque>
#include <iostream>
#include <sstream>
#include <string>
#include <iomanip>
constexpr int RX = 0, TX = 1, SERIAL_8N1 = 0, HEX = 16;
static uint32_t clockMs;
uint32_t millis() { return clockMs; }
struct SerialStandIn {
    std::deque<uint8_t> input;
    void begin(int, int, int, int) {}
    int available() { return static_cast<int>(input.size()); }
    int read() { if (input.empty()) return -1; int b = input.front(); input.pop_front(); return b; }
    template<class T> void print(T value) { std::cout << value; }
    void print(uint8_t value, int) { std::cout << std::uppercase << std::hex << unsigned(value) << std::dec; }
    void println() { std::cout << '\n'; }
    template<class T> void println(T value) { print(value); println(); }
} Serial;
"""
DRIVER = r"""
int main() {
    setup();
    std::string command;
    while (std::getline(std::cin, command)) {
        std::istringstream args(command);
        char action; args >> action;
        if (action == 't') {
            uint64_t ms; args >> ms; clockMs = static_cast<uint32_t>(ms); loop();
        } else if (action == 'b') {
            unsigned b;
            while (args >> std::hex >> b) Serial.input.push_back(static_cast<uint8_t>(b));
            do { loop(); } while (Serial.available());
        } else {
            return 2;
        }
    }
}
"""


@pytest.fixture(scope="module", params=["SerialLineReceiver", "SerialPacketReceiver"])
def receiver(request, tmp_path_factory):
    compiler = shutil.which("c++")
    if not compiler:
        pytest.skip("host C++ compiler unavailable")
    name = request.param
    work = tmp_path_factory.mktemp(name)
    source = REPO / "libraries/CH32RV/examples" / name / f"{name}.ino"
    driver = work / "driver.cpp"
    driver.write_text(PRELUDE + f'\n#include "{source.as_posix()}"\n' + DRIVER)
    binary = work / "receiver"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined,bounds", str(driver), "-o", str(binary)], check=True)

    def run(commands):
        result = subprocess.run([str(binary)], input="\n".join(commands) + "\n",
                                text=True, capture_output=True, check=True)
        assert not result.stderr
        return result.stdout.splitlines()[1:]  # Drop setup's instructions.

    return name, run


def send(data):
    return "b " + data.hex(" ")


def test_chunked_and_concatenated_messages(receiver):
    name, run = receiver
    if name == "SerialLineReceiver":
        message = b"  hello  \r\n"
        expected = "Line:   hello  "
    else:
        # Embedded zero and start marker must remain payload bytes.
        message = bytes.fromhex("A5 03 00 A5 FF 59")
        expected = "Packet bytes: 0 A5 FF"
    assert run([send(bytes([b])) for b in message]) == [expected]
    assert run([send(message * 2)]) == [expected, expected]


def test_capacity_and_recovery(receiver):
    name, run = receiver
    if name == "SerialLineReceiver":
        assert run([send(b"a" * 31 + b"\n")]) == ["Line: " + "a" * 31]
        assert run([send(b"a" * 100 + b"\nnext\n\n")]) == [
            "Line too long; discarded", "Line: next", "Line: "]
    else:
        maximum = bytes.fromhex("A5 10") + bytes(range(16)) + b"\x10"
        assert run([send(maximum)]) == ["Packet bytes:" + "".join(f" {i:X}" for i in range(16))]
        assert run([send(bytes.fromhex("A5 00 A5 11 A5 01 42 00 A5 01 42 43"))]) == [
            "Invalid length", "Invalid length", "Checksum mismatch", "Packet bytes: 42"]


def test_timeout_and_clock_wrap(receiver):
    name, run = receiver
    if name == "SerialLineReceiver":
        partial, rest = b"old", b"new\n"
        expected = ["Input timed out; partial line discarded", "Line: new"]
    else:
        partial, rest = bytes.fromhex("A5 03 10"), bytes.fromhex("A5 01 42 43")
        expected = ["Packet timed out", "Packet bytes: 42"]
    # No timeout at 999 ms; timeout at 1000 ms, across the 32-bit wrap.
    assert run(["t 4294967040", send(partial), "t 743", "t 744", send(rest)]) == expected
    # A fresh byte before the deadline extends the idle timeout.
    if name == "SerialLineReceiver":
        assert run([send(b"a"), "t 999", send(b"b"), "t 1500", send(b"\n")]) == ["Line: ab"]
    else:
        assert run([send(bytes.fromhex("A5 01")), "t 999", send(b"\x42"),
                    "t 1500", send(b"\x43")]) == ["Packet bytes: 42"]
