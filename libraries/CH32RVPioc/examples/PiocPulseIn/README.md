# PiocPulseIn

[日本語](README.ja.md)

`pulseIn()` by the PIOC: `pulse_in.S` counts one period of the signal on IO0 (the high and the low after it), so the
main core's interrupts (USB, `millis()`) do not shake the measurement. Every 0.5 s it prints the high, the low and the
frequency on `Serial`.

## What you need and wiring

A chip with a PIOC (X035 / X033, V205, H41x). The signal to IO0's pad (the sketch's `IO0_PIN`: PA0 on CH32H417, PC7
on CH32X035, PB10 on CH32V205), GND shared, at 3.3 V levels.

## Operation and expected result

A 1 kHz, 25 % signal prints `high 250000 ns, low 750000 ns, 1000.0 Hz`; without one, `no signal`.

## API and flow

- On `command()` the program waits for IO0 low, then counts from the rising edge while it stays high, then while it
  stays low.
- A turn of either count is 8 cycles, carry or not; the counts are 24 bits in d1 .. d3 (high) and d4 .. d6 (low), and
  `count x 8 / CH32RVPioc::clockHz()` is the time: 167 ns a turn at 48 MHz, up to about 2.8 s.
- Without edges the program waits on: when `waitDone()` times out, `begin()` again stops it.

On H417, X035 (48 MHz) and V205 the probe's PWM at 1 kHz 25 %, 10 kHz 50 % and 40 kHz 75 % came out within 0.5 % of
what the probe made, high and low (2026-11-19, pioc::test_pioc_pulse_in; judged at 2 % and 3 turns).
