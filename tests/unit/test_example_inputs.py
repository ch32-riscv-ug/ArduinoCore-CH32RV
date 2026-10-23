"""Exercise the actual input sketches with sampled pins, a clock and a HID sink.

Report types come from the real library. USB startup/events and send acceptance
are stand-ins: these checks do not cover enumeration, ADC accuracy or wiring.
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
BASIC = ["ButtonDebounce", "AnalogInputToPwm"]
USB = ["ButtonKeyboard", "ButtonMouse", "AnalogGamepad"]
PRELUDE = r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "CH32RVUsbDeviceTypes.h"
constexpr int HIGH=1, LOW=0, INPUT_PULLUP=2, OUTPUT=3, RX=4, TX=5, SERIAL_8N1=0;
constexpr uint8_t A0=0, A1=1, PA1=1, PB0=16;
static uint32_t clockMs;
static int pinLevels[32], adcValues[32], pwmValue;
uint32_t millis() { return clockMs; }
void delay(uint32_t ms) { clockMs += ms; }
void pinMode(int, int) {}
int digitalRead(int pin) { return pinLevels[pin]; }
int analogRead(int pin) { return adcValues[pin]; }
void analogReadResolution(int bits) { assert(bits == 12); }
void analogWriteResolution(int bits) { assert(bits == 8); }
void analogWrite(int, int value) { pwmValue=value; }
struct SerialSink {
    std::ostringstream output;
    void begin(int, int, int, int) {}
    template<class T> void print(T value) { output << +value; }
    void print(const char *text) { output << text; }
    template<class T> void println(T value) { print(value); output << '\n'; }
} Serial;
enum class CH32RVUsbBusEvent { Reset, Configured, Unconfigured, Suspended, Resumed };
struct CH32RVUsbDeviceConfig { const char *product=nullptr, *serialNumber=nullptr; };
class CH32RVUsbDevice;
static CH32RVUsbDevice *activeDevice;
static bool allowBegin=true, allowSend=true;
static std::vector<CH32RVUsbBusEvent> events;
class CH32RVUsbDevice {
public:
    bool online=false;
    void (*callback)(CH32RVUsbBusEvent)=nullptr;
    CH32RVUsbDevice() { activeDevice=this; }
    bool begin(const CH32RVUsbDeviceConfig &) { return allowBegin; }
    bool mounted() const { return online; }
    void onBusEvent(void (*cb)(CH32RVUsbBusEvent)) { callback=cb; }
    const char *lastErrorName() const { return "NotReady"; }
};
void deliverEvents() {
    for (auto event: events) if (activeDevice->callback) activeDevice->callback(event);
    events.clear();
}
struct Observation { bool pressed; int x, y; };
static std::vector<Observation> attempts, accepted;
bool capture(Observation value, uint32_t timeout) {
    assert(timeout == 0 && activeDevice->online);
    attempts.push_back(value);
    if (!allowSend) return false;
    accepted.push_back(value); return true;
}
class CH32RVUsbDeviceHidKeyboard {
public:
    explicit CH32RVUsbDeviceHidKeyboard(CH32RVUsbDevice &) {}
    bool sendReport(const CH32RVUsbDeviceBootKeyboardReport &r, uint32_t timeout) {
        assert(r.modifiers == 0 && r.reserved == 0);
        assert(r.keys[0] == 0 || r.keys[0] == CH32RV_USB_HID_KEY_A);
        for (int i=1; i<6; ++i) assert(r.keys[i] == 0);
        return capture({r.keys[0] != 0, 0, 0}, timeout);
    }
};
class CH32RVUsbDeviceHidMouse {
public:
    explicit CH32RVUsbDeviceHidMouse(CH32RVUsbDevice &) {}
    bool sendReport(const CH32RVUsbDeviceBootMouseReport &r, uint32_t timeout) {
        assert(r.x == 0 && r.y == 0 && r.wheel == 0);
        assert(r.buttons == 0 || r.buttons == CH32RV_USB_DEVICE_MOUSE_LEFT);
        return capture({r.buttons != 0, 0, 0}, timeout);
    }
};
class CH32RVUsbDeviceHidGamepad {
public:
    explicit CH32RVUsbDeviceHidGamepad(CH32RVUsbDevice &) {}
    bool sendReport(const CH32RVUsbDeviceGamepadReport &r, uint32_t timeout) {
        assert(r.z == 0 && r.rz == 0 && r.rx == 0 && r.ry == 0);
        assert(r.hat == CH32RV_USB_DEVICE_GAMEPAD_HAT_CENTER);
        assert(r.buttons == 0 || r.buttons == 1);
        return capture({r.buttons != 0, r.x, r.y}, timeout);
    }
};
"""
DRIVER = r"""
static void tick(uint32_t ms) { clockMs=ms; loop(); }
int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string scenario=argv[1];
    std::fill(std::begin(pinLevels), std::end(pinLevels), HIGH);
    std::fill(std::begin(adcValues), std::end(adcValues), 2048);
#if defined(EXAMPLE_ButtonDebounce)
    if (scenario == "held") pinLevels[BUTTON_PIN]=LOW;
    setup();
    if (scenario == "wrap") {
        pinLevels[BUTTON_PIN]=LOW; tick(0xFFFFFFF0u); tick(8);
        assert(pressCount == 0); tick(9); assert(pressCount == 1);
    } else if (scenario == "held") {
        tick(30); assert(pressCount == 0);
        pinLevels[BUTTON_PIN]=HIGH; tick(40); tick(65);
        pinLevels[BUTTON_PIN]=LOW; tick(70); tick(95); assert(pressCount == 1);
    } else {
        pinLevels[BUTTON_PIN]=LOW; tick(100);
        pinLevels[BUTTON_PIN]=HIGH; tick(105);
        pinLevels[BUTTON_PIN]=LOW; tick(110); tick(134);
        assert(pressCount == 0); tick(135); assert(pressCount == 1);
        tick(500); assert(pressCount == 1);
        pinLevels[BUTTON_PIN]=HIGH; tick(510); tick(535);
        assert(Serial.output.str().find("Released") != std::string::npos);
    }
#elif defined(EXAMPLE_AnalogInputToPwm)
    setup();
    adcValues[INPUT_PIN]=0; tick(10); assert(pwmValue == 0);
    adcValues[INPUT_PIN]=4095; tick(20); assert(pwmValue == 255);
    adcValues[INPUT_PIN]=2048; tick(30); assert(pwmValue == 127);
#else
    if (scenario == "begin_failure") allowBegin=false;
#if defined(EXAMPLE_AnalogGamepad)
    if (scenario == "calibration_failure") adcValues[X_PIN]=0;
#endif
    setup();
    if (scenario == "begin_failure" || scenario == "calibration_failure") {
        assert(!usbStarted);
        activeDevice->online=true; tick(1000);
        assert(attempts.empty());
        return 0;
    }
#if defined(EXAMPLE_AnalogGamepad)
    if (scenario == "axes") {
        for (int center: {65, 1000, 2048, 4030}) {
            assert(axisValue(0,center) == -127);
            assert(axisValue(4095,center) == 127);
            assert(axisValue(center-DEAD_ZONE,center) == 0);
            assert(axisValue(center+DEAD_ZONE,center) == 0);
            int previous=-127;
            for (int raw=-1; raw<=4096; ++raw) {
                const int mapped=axisValue(raw,center);
                assert(mapped >= previous && mapped >= -127 && mapped <= 127);
                previous=mapped;
            }
        }
        activeDevice->online=true; events.push_back(CH32RVUsbBusEvent::Configured);
        adcValues[X_PIN]=0; adcValues[Y_PIN]=4095; tick(1000);
        assert(accepted.back().x == -127 && accepted.back().y == 127);
        return 0;
    }
#endif
    tick(900); assert(attempts.empty()); // No reports before enumeration.
    activeDevice->online=true; events.push_back(CH32RVUsbBusEvent::Configured); tick(1000);
    assert(!accepted.back().pressed);
    pinLevels[BUTTON_PIN]=LOW; tick(1010); tick(1034);
    assert(!accepted.back().pressed); tick(1035); tick(1045); assert(accepted.back().pressed);
    const size_t heldCount=accepted.size(); tick(1050);
#if !defined(EXAMPLE_AnalogGamepad)
    assert(accepted.size() == heldCount); // No artificial key/button repeats.
#else
    (void)heldCount;
#endif
    // A failed release must remain pending until a later accepted neutral report.
    allowSend=false; pinLevels[BUTTON_PIN]=HIGH; tick(1100); tick(1125);
    assert(!attempts.back().pressed && accepted.back().pressed);
    allowSend=true; tick(1135); assert(!accepted.back().pressed);
    pinLevels[BUTTON_PIN]=LOW; tick(1200); tick(1225); assert(accepted.back().pressed);
    activeDevice->online=false; events.push_back(CH32RVUsbBusEvent::Reset); tick(1300);
    activeDevice->online=true; events.push_back(CH32RVUsbBusEvent::Configured); tick(1400);
    assert(!accepted.back().pressed); tick(1450); assert(!accepted.back().pressed);
    // Even a reset/reconfigure that happens between loops must disarm a held button.
    events.push_back(CH32RVUsbBusEvent::Reset); events.push_back(CH32RVUsbBusEvent::Configured);
    tick(1460); assert(!accepted.back().pressed);
    pinLevels[BUTTON_PIN]=HIGH; tick(1500); tick(1525);
    pinLevels[BUTTON_PIN]=LOW; tick(1600); tick(1625); assert(accepted.back().pressed);
#endif
}
"""


@pytest.fixture(scope="module")
def build_input(tmp_path_factory):
    compiler = shutil.which("c++")
    if not compiler:
        pytest.skip("host C++ compiler unavailable")
    binaries = {}

    def build(name):
        if name not in binaries:
            work = tmp_path_factory.mktemp(name)
            (work / "CH32RVUsbDevice.h").write_text("// API stand-ins supplied by driver.cpp\n")
            lib = "CH32RV" if name in BASIC else "CH32RVUsbDevice"
            ino = REPO / "libraries" / lib / "examples" / name / f"{name}.ino"
            driver = work / "driver.cpp"
            driver.write_text(PRELUDE + f'\n#include "{ino.as_posix()}"\n' + DRIVER)
            binary = work / "run"
            result = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined,bounds", f"-DEXAMPLE_{name}",
                            f"-I{work}", "-isystem", str(REPO / "libraries/CH32RVUsbDevice/src"),
                            str(driver), "-o", str(binary)], capture_output=True, text=True)
            assert result.returncode == 0, result.stdout + result.stderr
            binaries[name] = binary
        return binaries[name]

    return build


@pytest.mark.parametrize("name", BASIC + USB)
def test_input_states_and_send_recovery(build_input, name):
    subprocess.run([str(build_input(name)), "normal"], check=True)


@pytest.mark.parametrize("scenario", ["held", "wrap"])
def test_button_startup_and_wrap(build_input, scenario):
    subprocess.run([str(build_input("ButtonDebounce")), scenario], check=True)


@pytest.mark.parametrize("name", USB)
def test_failed_usb_start_sends_no_reports(build_input, name):
    subprocess.run([str(build_input(name)), "begin_failure"], check=True)


@pytest.mark.parametrize("scenario", ["axes", "calibration_failure"])
def test_gamepad_calibration_and_mapping(build_input, scenario):
    subprocess.run([str(build_input("AnalogGamepad")), scenario], check=True)
