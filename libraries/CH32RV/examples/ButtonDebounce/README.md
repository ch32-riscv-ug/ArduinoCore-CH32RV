# ButtonDebounce

[日本語](README.ja.md)

Observe one press and one release despite the brief contact changes produced by a mechanical switch. This polling example is the starting point for the USB button examples.

## Wiring and operation

Connect a normally open momentary switch between `BUTTON_PIN` (initially `PA1`) and GND. `INPUT_PULLUP` keeps a released switch High; pressing makes it Low. Do not wire the button to an unrelated supply. Use a different available input if PA1 is occupied on your board.

Open the UART terminal, then press, hold and release. A new stable press prints `Pressed, count: ...` once. Holding does not increment the count again; releasing prints `Released`. A switch held at startup is taken as the initial state and does not count as a new press. Release it, then press again to count.

## API and processing

`digitalRead()` samples the electrical level. `candidateLevel` records the latest raw level and `changedAtMs` records when it changed. Only after 25 ms without another change does it become `stableLevel`. A separate `reportedLevel` detects transitions, so a held level is not mistaken for repeated presses.

`millis()` subtraction works across its counter wrap. There is no debounce `delay()`: other short work can run each loop. This is software filtering for a human-operated button, not a pulse counter; short pulses and intermediate changes between samples can be missed. Keep the loop responsive and choose the debounce interval to suit your switch. `pressCount` is RAM state and starts over after reset.

Continue with [ButtonKeyboard](../../../CH32RVUsbDevice/examples/ButtonKeyboard/README.md) or [ButtonMouse](../../../CH32RVUsbDevice/examples/ButtonMouse/README.md) to turn the debounced state into host input.

## Board and UART setup

Choose the board and exact Part Number, and check each pin against the schematic. Generic aliases describe the variant, not PCB connector labels. Avoid UART, upload/debug and other peripheral pin conflicts. Generated `sketch.yaml` profiles describe build requirements, not verified wiring.

Diagnostics use hardware `Serial` at 115200 baud, 8N1: adapter RX ← board TX, adapter TX → board RX, common GND. Match I/O levels. This UART is separate from USB HID and does not automatically appear through the board's USB connector.

See [example build rules](../../../../docs/examples-build-rules.ja.md), [upload routes](../../../../docs/upload-routes.ja.md) and the [example index](../../../../docs/examples.md).
