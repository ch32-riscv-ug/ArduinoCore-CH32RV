# PpsVoltageControl

日本語: [README.ja.md](README.ja.md)

Sets the voltage of a USB PD charger's PPS range (a range chosen in 20 mV steps) from the UART, in mV. Handles a charger
without PPS, a voltage outside the range, a refusal and unplugging.

## What you need and the wiring

A series with USBPD such as CH32X035 (the CC pins are in the library's README), a PD charger with PPS, a UART monitor.
**The voltage really changes**: check first that the board's VBUS input and the load stand the top of the charger's
PPS range. The current asked for is `REQUEST_MA` (1000 mA by default).

## What to do and what to expect

1. With the charger plugged in, the contract's voltage and the PPS range show:

   ```
   charger ready at 5000 mV
   PPS profile 4: 3300..11000 mV, up to 3000 mA
   type a voltage in mV and Enter
   ```

2. Type `7400` and Enter: `contract: 7400 mV, up to 1000 mA (PPS)`. Check VBUS with a meter.
3. Outside the range (`20000` and so on): `20000 mV is outside 3300..11000 mV`. A refusal: `refused; still ... mV`.
4. A charger without PPS: `this charger has no PPS range (fixed voltages only)`. Unplugged: `charger unplugged`.

## The API and the flow

| API | Its use here |
|---|---|
| `ready()` / `connected()` | A contract or not, and unplugged or renegotiating. |
| `profileCount()` / `profile(i).kind` | Finds the `CH32RVUsbPdSupply::Pps` range in the advertisement and reads `min_mv` / `max_mv`. |
| `requestProfile(index, mV, mA)` | Asks for a voltage in that range. It waits for the answer (up to about 1.5 s); a refusal is false and the previous contract stands. |
| `voltage()` / `current()` | The accepted contract's values, not measurements. |

The driver asks for the PPS contract again every 5 s so the charger does not drop it (from `delay()` and the return of
`loop()`).
