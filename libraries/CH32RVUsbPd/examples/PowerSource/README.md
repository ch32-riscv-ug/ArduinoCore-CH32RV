# PowerSource

日本語: [README.ja.md](README.ja.md)

A PD source protocol template with callbacks for a VBUS switch and adjustable supply. As supplied it only logs; it does not control a physical supply.

## Hardware and connections

A series with USBPD (CH32X035 and others; CC pads in the library README) source CC circuitry, a current-limited supply, VBUS switch, programmable regulator and UART monitor. On WeAct X035 the sink Rd resistors R4/R5 must be removed for source operation.

## Steps and expected results

1. Replace vbus and setVoltage with real power-circuit control, returning true only after switching completes.
2. Change advertised 5 V/3 A, 9 V/2 A and PPS 3.3–11 V/2 A to actual circuit capabilities.
3. Connect a sink and inspect VBUS-on, voltage and contract logs.
4. Verify VBUS-off on detach and return to 5 V on reset/failure using the physical circuit.

## Settings and limits

The supplied callbacks always return true after printing. Implement real switching before connecting a sink: otherwise PS_RDY is sent without the requested voltage being applied. setRpCurrent does not make a supply capable of 3 A. USBPD sink and USBPDSource cannot own the PD block simultaneously.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `setRpCurrent(...)` | Advertises pre-contract current over CC; it does not set load current. |
| `addFixed(mv, ma)` / `addPps(minMv, maxMv, ma)` | Register deliverable capabilities, starting with fixed 5 V. Check registration results. |
| `onVbus(callback)` | Controls VBUS on/off; return `true` once the requested physical state is reached. |
| `onSetVoltage(callback)` | Controls contract voltage/current capability; return `false` on failure. |
| `attached()` / `ready()` | Distinguish sink connection from an explicit contract. |

These callbacks answer whether the driver may proceed; they are not deferred notifications. Do not return `true` merely because a change was scheduled. Nor does `false` mean "call me again later"; check recovery behavior in the [library README](../../README.md).

Verify initial 5 V, voltage changes, detach and reset with the physical circuit. The sample prints PDO indices plus one, unlike zero-based API indices. Do not recursively invoke PD requests from supply callbacks.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbPd API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[PowerSource.ino](PowerSource.ino)
