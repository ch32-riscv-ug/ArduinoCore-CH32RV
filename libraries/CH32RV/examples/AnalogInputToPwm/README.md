# AnalogInputToPwm

[日本語](README.ja.md)

Turn a potentiometer to adjust an LED's PWM duty, connecting the separate `AnalogRead` and `Fade` lessons.

## Wiring and operation

Connect a potentiometer's ends to the board's analog-compatible supply and GND, with its wiper to `INPUT_PIN` (`A0`). Use a value suitable for the ADC input; a roughly 10 kΩ potentiometer is a practical starting point. Keep the wiper voltage inside the pin's allowed range. The analog supply is not necessarily USB's 5 V.

Connect `PWM_PIN` (`PA1`) through a suitable resistor and LED to GND. Use an actual PWM output separate from the analog input, UART and USB pins. Do not drive a motor or other power load directly. If the board's LED is active Low, duty and perceived brightness are inverted.

Turn the knob: readings and duty should increase toward the supply end, and LED brightness should change. UART prints the ADC and PWM values twice per second. Verify that both endpoint connections and the wiper are correct if the values float or remain at a rail.

## API and adaptation

`analogReadResolution(12)` selects 0–4095 for reading and `analogWriteResolution(8)` selects 0–255 for writing. These are independent numeric scales. The expression `raw * 255 / 4095` uses a 32-bit intermediate. It converts scales, not physical volts; requesting more ADC bits does not add accuracy.

The loop samples every 10 ms and prints less frequently using `millis()` differences. `analogWrite()` changes duty without converting the output to a DAC voltage. Small ADC noise may cause brightness changes; add averaging or hysteresis if your application needs steadier output. For frequency and timer sharing, see [PwmFrequency](../PwmFrequency/README.md).

## Board and UART setup

Choose the board and exact Part Number, and check each pin against the schematic. Generic aliases describe the variant, not PCB connector labels. Avoid UART, upload/debug and other peripheral pin conflicts. Generated `sketch.yaml` profiles describe build requirements, not verified wiring.

Diagnostics use hardware `Serial` at 115200 baud, 8N1: adapter RX ← board TX, adapter TX → board RX, common GND. Match I/O levels. This UART is separate from USB HID and does not automatically appear through the board's USB connector.

See [example build rules](../../../../docs/examples-build-rules.ja.md), [upload routes](../../../../docs/upload-routes.ja.md) and the [example index](../../../../docs/examples.md).
