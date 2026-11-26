# SerialHalfDuplex

[日本語](README.ja.md)

Single-wire half duplex: `Serial1` sends and receives on its TX pad alone (open drain), as servo buses and some
sensors do. The board sends `ping` every second and prints whatever else arrives on the wire.

## Hardware and connections

The device's single data line to `Serial1`'s default TX pad (`CH32RV_SERIAL2_TX`, USART2's - Serial1 on most boards),
a pull-up resistor (4.7-10 kOhm) from that line to 3.3 V, common GND. `Serial` (115200) shows the replies.

## Steps and expected results

The board prints `One-wire ready`, then sends `ping` every second; replies appear on `Serial`. Without a pull-up the
open-drain line never rises and nothing is sent or received.

## API and processing flow

`Serial1.begin(9600, SERIAL_8N1, WIRE_PIN, WIRE_PIN)` (the same pin for RX and TX, ESP32's one-wire form) sets the
USART's single-wire mode (HDSEL) on the route whose TX that pad is; `setPins(p, p)` does the same. The receiver is
joined to the line inside, but it does not receive while the USART sends: unlike ESP32's one-wire, the board's own
bytes do not come back, and only the device's answer is read.

## Adapting the example

CH32X033 / X035 have no open-drain pads: there the pad stays push-pull and drives the idle line high, so it must not
share the wire with another driver. Change the speed and the query for your device.

## Related documentation

- [Sketch](SerialHalfDuplex.ino)
- [Basic example setup and index](../../README.md)
