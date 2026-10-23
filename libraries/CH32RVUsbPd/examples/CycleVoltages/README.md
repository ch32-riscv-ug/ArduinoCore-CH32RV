# CycleVoltages

日本語: [README.ja.md](README.ja.md)

A button cycles through the charger fixed-voltage PDOs and logs contract values.

## Hardware and connections

A series with USBPD (CH32X035 and others; CC pads in the library README), a PD charger, a PA1-to-GND button (PA1 is CC2 on CH32V205: move `BUTTON_PIN`) and UART monitor. Board input power circuitry and loads must tolerate the highest fixed voltage offered.

## Steps and expected results

1. Check `contract: ... mV, up to ... mA` after connection.
2. Press PA1 to request the next fixed PDO, wrapping at the end.
3. Check the negotiated result on UART and optionally compare with external VBUS measurement.
4. A failed request prints `the charger refused`.

## Settings and limits

PPS, battery and variable PDOs are skipped. The INPUT_PULLUP button is processed on its falling edge without dedicated debounce; add it if one press skips multiple entries. The driver keeps the link from delay() and the return of loop().

## API usage and adaptation

| API | Use in this example |
|---|---|
| `profileCount()` / `profile(index).kind` | Find fixed PDOs in the advertised list with zero-based indices. |
| `contractProfile()` | Gets the selected PDO index. Check `ready()` first because a contract may not exist. |
| `requestProfile(index)` | Requests that fixed PDO; PPS additionally needs a voltage. |
| `voltage()` / `current()` | Report the successful contract, not a physical voltage measurement. |

The sample detects a High-to-Low transition to avoid requesting on every iteration while held. Add debounce for contact chatter.

Requests wait for negotiation and must not run in an ISR. Confirm a fixed PDO was found and decide whether failures await another press or retry. PDO order can change between sources; do not persist an index as a permanent identifier for 9 V.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbPd API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[CycleVoltages.ino](CycleVoltages.ino)
