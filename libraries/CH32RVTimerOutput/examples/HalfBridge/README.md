# HalfBridge

[日本語](README.ja.md)

Drive a half bridge's two sides from one channel of an advanced timer (`CH32RVComplementaryPwm`): CHx is the high side, CHxN the low side (CHx inverted), with a dead time where both are off around each switch, and a break input that turns both off in hardware.

## Hardware and connections

**Do not connect a power stage until the waveforms look right on a logic analyser or an oscilloscope.** The sketch picks the pins itself and prints them at start (`High side PA8, low side PB13, dead time 500 ns`). Watch both: they never are high at the same time, and the gap between one going low and the other going high is the dead time. If the chip has the timer's break input on the same route, it is printed too (`Break input PB12`): connecting it to GND turns both outputs low at once.

## Steps and expected results

The duty sweeps slowly up and down at 20 kHz. Grounding the break input prints `Break: both sides off`; releasing it lets `resume()` start the outputs again (`Resumed`).

## API and adaptation

- `PWM_HZ` and `DEAD_NS` set the frequency and the dead time. `deadTime()` says what was set: the timer's dead-time field has steps of its clock, coarser for long ones (up to 1008 clocks).
- `write(duty)` takes 0 to 256 here (`begin(..., 8)` - 8 bits; 256 is 100 %).
- Both outputs are active high and go low on a break or `end()`. Gate drivers that want another polarity need an inverter, or another timer feature this library does not cover yet.

## Before running

Output is hardware `Serial` at 115200 baud, 8N1. The generated `sketch.yaml` supplies build profiles. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
