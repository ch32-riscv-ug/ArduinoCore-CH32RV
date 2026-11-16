# SoftwareSerial

[日本語](README.ja.md)

A UART on any two pads, with Arduino's SoftwareSerial API (the AVR library's
`begin`, `listen`, `isListening`, `stopListening`, `overflow`, `peek`, `read`,
`write`, `available`, `flush`). For when the hardware USARTs' routes do not
reach the pads, or there are not enough of them - CH32V003 has one.

```cpp
#include <SoftwareSerial.h>

SoftwareSerial gps(PC1, PC2);      // RX, TX: any pads

void setup() {
  gps.begin(9600);                 // 8N1
  // gps.begin(9600, SERIAL_8E1);  // HardwareSerial's formats
}

void loop() {
  while (gps.available()) Serial.write(gps.read());
}
```

## How it works

- **Receive.** The start bit's edge raises the RX pad's interrupt
  (`attachInterrupt()`), and the handler reads the rest of the frame there,
  each bit in its middle, timed by SysTick. The time the interrupt takes to
  arrive is measured by `listen()` (a software edge on the same EXTI line) and
  counted back, so the sampling does not depend on the chip or its clock. The
  bytes go to a 64-byte buffer (`_SS_MAX_RX_BUFF`) shared by all objects:
  only the listening one receives, as on AVR. A full buffer drops the byte and
  `overflow()` says so; a frame with a framing or parity error is dropped.
- **Transmit.** `write()` sends the frame bit by bit with interrupts off (the
  stop bit with them on) and returns when it is out; `flush()` has nothing to
  wait for.
- **Half duplex**, as on AVR: a byte that arrives while one is sent is lost,
  and every other interrupt (Serial, USB, `millis()`) waits up to one frame.
  The receiving handler holds the CPU for one frame too, and one bit more
  watching for a next start bit: frames that follow each other are read in
  the same interrupt, timed from the edge it sees (a fresh interrupt comes
  too late on the slower parts at 115200).
- **Formats.** `begin(speed, config)` takes `SERIAL_5N1` .. `SERIAL_8O2`:
  5-8 data bits, none / even / odd / mark / space parity, 1, 1.5 or 2 stop
  bits. `begin(speed)` is 8N1. `inverse_logic` (the constructor's third
  argument) idles low.
- **Pins.** Any pad. The RX pad takes the EXTI line of its bit number while
  listening: another pin with the same bit number cannot use
  `attachInterrupt()` meanwhile. A pin that is no pad (255) leaves that side
  out (send only or receive only).

## Speeds

The receive side needs the interrupt to arrive within one and a half bits of
the edge; CH32X035 at 48 MHz takes about 5 us, so 115200 (8.7 us a bit) is the
practical top on the slow parts. The bench test (`uart.software-serial`) runs
9600, 57600 and 115200 8N1 and 8E1, 8O1, 8N2, 7E1 at 9600, both ways, against
the probe's UART. Receiving at 9600 is required; faster receives are recorded,
not promised (they depend on the part's interrupt latency). On the bench
(2026-11-17) V003, X035, V203, V205 and H417 received at 115200; V006 (24 MHz)
got 7 bytes of 16 there. Sending was right on every part at every speed.

## Differences from AVR's

- `begin(speed, config)` is added for the formats.
- The bit timing comes from SysTick, not from cycle-counted delay tables, so
  every F_CPU works.

## Examples

- [SoftwareSerialExample](examples/SoftwareSerialExample/README.md): bridge
  `Serial` and a SoftwareSerial.
