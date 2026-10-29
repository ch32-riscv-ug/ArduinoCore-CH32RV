# AnalogGamepad

[日本語](README.ja.md)

Use a two-axis analog joystick or two potentiometers and a button as a USB gamepad. This follows [AnalogInputToPwm](../../../CH32RV/examples/AnalogInputToPwm/README.md) and [ButtonDebounce](../../../CH32RV/examples/ButtonDebounce/README.md), combining analog scaling and stable button state.

## Wiring and calibration

Connect each potentiometer's ends to the ADC-compatible supply and GND, and the wipers to `X_PIN` (`A0`) and `Y_PIN` (`A1`). A joystick module must provide analog outputs in the board's permitted input range. Connect a normally open button between `BUTTON_PIN` (`PB0`) and GND. All grounds must be common. Resolve the aliases to actual pads and avoid UART/USB/debug conflicts. A module powered from USB 5 V is not automatically safe for these ADC inputs.

Leave both axes centered before reset or power-up. Setup requests 12-bit ADC readings and averages 32 readings for each center. It does not save calibration in Flash. An axis near either rail rejects startup with an UART message: check wiring and center the controls, then reset. Two ordinary potentiometers need to be set near the middle manually; the sketch cannot determine their mechanical center itself.

Open the OS game-controller test panel. At rest, X/Y should be neutral. Move each axis toward both ends and check approximately −127 to +127 in raw-report terms; host panels may display a different scale. Press and release the button to operate button one after its initial stable release. Other axes stay at zero and the hat stays centered.

## Mapping and transmission

The center dead zone is 64 raw ADC counts on each side. `axisValue()` scales the remaining positive and negative sides independently; it uses 32-bit arithmetic and clamps raw input to the selected ADC range. Center validation prevents a zero mapping denominator. The dead zone reduces small center jitter, not noise across the entire axis range. To invert an axis, negate its mapped value before putting it in the report.

`CH32RVUsbDeviceGamepadReport` represents current state. `buttons = 1UL` sets button one, and zero releases it. The loop sends a full report every 10 ms, with unused fields left at their neutral defaults. If `sendReport(report, 0)` fails, the next interval samples fresh input and tries again; it does not accumulate outdated axis positions. Button debounce uses 25 ms independently of the report interval. Reconnecting disarms the button until release; axes continue to reflect their current calibrated position.

For calibration that drifts or asymmetric travel, adjust the dead zone or add explicit minimum/maximum calibration. Startup averaging is not a precision voltage measurement or a full joystick calibration procedure.

## USB state and report API

The device and HID function are declared before `setup()`. `device.begin(config)` starts the stack; it does not mean the host has finished enumeration. `device.ready()` checks whether the host configured it. Use the MCU's USB data connector and a data cable; HID is not a serial port. See the [USB Device guide](../README.md) and [USB support table](../../../../docs/usb.ja.md#対応状況) for controller, clock and wiring requirements.

`deliverEvents()` runs the registered `onBusEvent()` notification from the loop. Reset or reconfiguration disarms button input. After a stable release, a new press can operate the host. Unplugging and reconnecting while holding the switch therefore does not create an immediate press. This does not implement remote wake from host sleep.

`sendReport(report, 0)` attempts a full-state report without waiting for a busy endpoint. Success means the report was accepted for USB transfer, not that a host application acted on it. The next loop can continue sampling. Short taps that finish while sending is unavailable may coalesce; the example tracks the latest state, not an event queue.

## Board and UART setup

Choose the board and exact Part Number, and check each pin against the schematic. Generic aliases describe the variant, not PCB connector labels. Avoid UART, upload/debug and other peripheral pin conflicts. Generated `sketch.yaml` profiles describe build requirements, not verified wiring.

Diagnostics use hardware `Serial` at 115200 baud, 8N1: adapter RX ← board TX, adapter TX → board RX, common GND. Match I/O levels. This UART is separate from USB HID and does not automatically appear through the board's USB connector.

See [example build rules](../../../../docs/examples-build-rules.ja.md), [upload routes](../../../../docs/upload-routes.ja.md) and the [example index](../../../../docs/examples.md).
