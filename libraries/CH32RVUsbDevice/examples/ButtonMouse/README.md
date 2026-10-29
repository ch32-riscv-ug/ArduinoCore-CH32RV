# ButtonMouse

[日本語](README.ja.md)

Use a physical switch to hold and release the left mouse button. Start with [ButtonDebounce](../../../CH32RV/examples/ButtonDebounce/README.md) to understand the input filtering.

## Connections and operation

Connect a normally open momentary button from `BUTTON_PIN` (`PA1`) to GND. The internal pull-up makes released High and pressed Low. Choose another valid GPIO if this conflicts with UART or your board's USB/debug wiring.

Position the pointer over a test area using your normal mouse. Release the switch after connection, then press, hold and release. The switch holds the left button while pressed. Releasing ends the hold. This example sends no pointer movement or wheel input; moving an ordinary mouse while the switch is held may drag an object.

The button bitmap uses `CH32RV_USB_DEVICE_MOUSE_LEFT`; zero releases every button represented by the report.

## State handling

The button is debounced for 25 ms. Press and release are both full reports; a `CH32RVUsbDeviceBootMouseReport` initializes movement and wheel to zero. Reports are attempted at least 10 ms apart when state changes or the connection needs initialization. The accepted-state record changes only after `sendReport()` succeeds. If sending a release fails, the loop retries the current released state rather than remembering it as sent. Error diagnostics are rate-limited.

Try a quick tap, long hold, and reconnect with the switch held. On reconnect the example sends a neutral state and waits for a stable release before accepting another press. It cannot release input while the cable is disconnected; host removal handling is responsible during that time.

## USB state and report API

The device and HID function are declared before `setup()`. `device.begin(config)` starts the stack; it does not mean the host has finished enumeration. `device.ready()` checks whether the host configured it. Use the MCU's USB data connector and a data cable; HID is not a serial port. See the [USB Device guide](../README.md) and [USB support table](../../../../docs/usb.ja.md#対応状況) for controller, clock and wiring requirements.

`deliverEvents()` runs the registered `onBusEvent()` notification from the loop. Reset or reconfiguration disarms button input. After a stable release, a new press can operate the host. Unplugging and reconnecting while holding the switch therefore does not create an immediate press. This does not implement remote wake from host sleep.

`sendReport(report, 0)` attempts a full-state report without waiting for a busy endpoint. Success means the report was accepted for USB transfer, not that a host application acted on it. The next loop can continue sampling. Short taps that finish while sending is unavailable may coalesce; the example tracks the latest state, not an event queue.

## Board and UART setup

Choose the board and exact Part Number, and check each pin against the schematic. Generic aliases describe the variant, not PCB connector labels. Avoid UART, upload/debug and other peripheral pin conflicts. Generated `sketch.yaml` profiles describe build requirements, not verified wiring.

Diagnostics use hardware `Serial` at 115200 baud, 8N1: adapter RX ← board TX, adapter TX → board RX, common GND. Match I/O levels. This UART is separate from USB HID and does not automatically appear through the board's USB connector.

See [example build rules](../../../../docs/examples-build-rules.ja.md), [upload routes](../../../../docs/upload-routes.ja.md) and the [example index](../../../../docs/examples.md).
