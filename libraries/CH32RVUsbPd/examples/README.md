# USB PD examples

[日本語](README.ja.md)

These examples use Type-C CC communication for PD sink/source operation, independently of USB data classes. Check supported parts and CC pins in the [library README](../README.md).

## Wiring and power

Check Type-C, CC and power circuits against the board schematic. Logs use the sketch's hardware UART, not CDC. Connect adapter RX to board TX, TX to RX, and common GND; match I/O voltage. Keep CC wiring short.

| Example | Role | Required equipment |
|---|---|---|
| ListProfiles | Sink | PD charger; requests 9 V after startup |
| CycleVoltages | Sink | PD charger and PA1-to-GND button; cycles fixed voltages |
| PowerSource | Source | Adjustable supply, VBUS switch, source CC circuit and real control callbacks |

The sink examples change VBUS voltage. A board powered from VBUS and its loads must tolerate the requested voltage. voltage/current report contract values, not measurements.

PowerSource callbacks only print. Implement actual switching and set advertised PDO/Rp currents to physical capabilities before connecting a sink. Remove sink Rd resistors as appropriate; on WeAct X035 these are R4/R5.

## Build and operation

From the repository root, with the core installed:

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035F8U6 \
  libraries/CH32RVUsbPd/examples/ListProfiles
```

Alternatively open File → Examples → CH32RVUsbPd in Arduino IDE. Generated profiles declare build conditions, not hardware-verified support.

Sink examples use a bounded wait for the initial contract. connected means CC attach; ready means an explicit PD contract. A Type-C-only supply may never become ready. Request units are mV/mA: 9 V is 9000. The driver keeps the link (and a PPS contract) alive from delay() and the return of loop(); nothing has to be called.

Only one sink/source role can own the PD block at a time. USB data-device operation is independent, but simultaneous use requires a peer supporting both power and data roles and suitable connector power wiring.

## Troubleshooting

| Symptom | Check |
|---|---|
| begin fails | A series with the block (not X033), required source callbacks, opposite-role Busy |
| connected but not ready | PD-capable source, CC wiring, wait time |
| request fails | Advertised PDO, mV/mA units, exact fixed voltage/PPS range and peer rejection |
| PPS expires | A loop() or callback that does not return for seconds |
| Reboot loop after reset | Contract recovery/Hard Reset dropping VBUS and board power routing |
| Negotiated and measured voltage differ | Physical source callback and supply implementation |

See the [library API](../README.md) for quantization, failure recovery and hardware verification coverage.

## Examples

| Example | Purpose |
|---|---|
| [CycleVoltages](CycleVoltages/README.md) | A button cycles through the charger fixed-voltage PDOs and logs contract values. |
| [ListProfiles](ListProfiles/README.md) | Lists charger PDOs and requests 9 V. This actively changes VBUS voltage rather than only inspecting capabilities. |
| [PowerSource](PowerSource/README.md) | A PD source protocol template with callbacks for a VBUS switch and adjustable supply. As supplied it only logs; it does not control a physical supply. |
