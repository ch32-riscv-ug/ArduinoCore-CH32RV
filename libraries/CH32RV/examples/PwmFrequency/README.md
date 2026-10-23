# PwmFrequency

[日本語](README.ja.md)

Compare three requested PWM frequencies while keeping the duty at approximately 25%. A scope or logic analyzer makes the difference visible.

## Connections and operation

Choose a PWM-capable `PWM_PIN` that is exposed on your board and does not conflict with UART. The initial `PA1` is an example, not a universal board LED. Connect the instrument input to that pin and share GND. Alternatively, connect an LED through a suitable current-limiting resistor; brightness alone will not reliably distinguish the frequencies.

The first output requests 100 Hz. Send `n` to cycle through 1000 Hz, 5000 Hz and back to 100 Hz. Confirm the period changes while the High fraction stays near one quarter.

## API and adaptation

`analogWriteResolution(8)` selects the numeric duty scale. `analogWrite(PWM_PIN, 64)` requests about one quarter of that scale. `analogWriteFrequency(pin, Hz)` changes frequency, then another `analogWrite()` applies it to the output.

In this core the frequency setting is shared; the pin argument does not create a per-pin frequency. Other outputs apply the shared setting on their subsequent writes, and channels using the same timer share its base period. Do not assume independent frequencies on adjacent PWM pins. Requested values can be rounded or limited by the hardware. Servo, tone and timer-owning libraries may also claim the timer; see [TimerResource](../TimerResource/README.md).

If there is no waveform, first check the selected part's pin capability and timer ownership. A plain GPIO without PWM capability is not a substitute for this experiment.

## Before running

Select your board and exact Part Number, then check the sketch's pins against its schematic. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. Use compatible I/O voltages. The board's USB connector does not automatically carry this UART.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
