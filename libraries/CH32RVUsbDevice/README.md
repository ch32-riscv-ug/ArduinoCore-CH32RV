# CH32RVUsbDevice — the chip's own USB as a device

CDC serial (`SerialUSB`), HID (keyboard, mouse, generic), MIDI and a DFU
runtime interface, combined by declaring them. Built on the vendored TinyUSB
([CH32RVTinyUSB](../CH32RVTinyUSB/)). The full guide (Japanese) is
[docs/usb.ja.md](../../docs/usb.ja.md).

```cpp
#include <CH32RVUsbDevice.h>

CH32RVUsbDeviceKeyboard keyboard;        // declaring it attaches it to USBDevice
CH32RVUsbDeviceDfuRuntime dfu;           // re-upload over USB alone

void setup() {
  CH32RVUsbDeviceConfig config;
  config.product = "My gadget";
  if (!USBDevice.begin(config)) {        // lays out the descriptors and connects
    // USBDevice.lastErrorName() says why
  }
  SerialUSB.begin();                     // SerialUSB is predefined and attached too
}
void loop() { if (SerialUSB) SerialUSB.println("hello"); delay(1000); }
```

## How it is put together

- **One device; functions join it by being declared.** `USBDevice` and
  `SerialUSB` are predefined like `Serial`; a sketch may declare its own
  `CH32RVUsbDevice` instead (one device per sketch). Functions attach in
  declaration order.
- **What is not used is not linked.** A class the sketch never declares brings
  no code and no memory; including the header alone costs nothing.
- **Only sizes are fixed at build time.** How many of a class a device can
  have and the buffer lengths can be raised per sketch from `build_opt.h`
  (e.g. `-DCFG_TUD_CDC=2`).
- **Failure is a return value.** When `begin()` returns false,
  `lastError()` / `lastErrorName()` say why. On a series without USB support
  the sketch still builds (`Unsupported`).

## Classes

| Class | What |
|---|---|
| `CH32RVUsbDevice` | The device: `begin(config)`, `mounted()`, `suspended()`, `remoteWakeup()`, `enterBootloader()` |
| `CH32RVUsbDeviceConfig` | VID/PID, manufacturer / product / serial strings, power, port |
| `CH32RVUsbDeviceCdcSerial` | A virtual COM port (`Stream`), 1200 bps touch, `onLineState()`, `onLineCoding()` |
| `CH32RVUsbDeviceKeyboard` | Boot keyboard: `print()`, `press()` / `release()`, `leds()` |
| `CH32RVUsbDeviceMouse` | Boot mouse: `move()`, `click()` |
| `CH32RVUsbDeviceHidGeneric` | Vendor-defined HID, 1..64-byte reports: `send()`, `onOutputReport()` |
| `CH32RVUsbDeviceHid` | HID with your own report descriptor |
| `CH32RVUsbDeviceMidi` | USB MIDI: `noteOn()`, `controlChange()`, `readPacket()` |
| `CH32RVUsbDeviceDfuRuntime` | DFU_DETACH resets into the factory USB bootloader; carries a WinUSB MS OS 2.0 descriptor for Windows |

## Examples

SerialUSBEcho, CustomDevice, TwoSerialPorts (`build_opt.h`), Keyboard, Mouse,
HidGenericEcho, MidiController, DfuRuntime.

## Support

CH32X035 / X033 (USBFS). Checked on a WeAct CH32X035F8U6 board: every example
enumerates and works, and `ch32rv isp flash` after a DFU_DETACH or a 1200 bps
touch. On other series `begin()` returns false.

## Uploading

A 1200 bps touch (with a CDC port) or a DFU_DETACH (with
`CH32RVUsbDeviceDfuRuntime`) resets the chip into its factory USB bootloader
(1a86:55e0), where `ch32rv isp flash` writes it; without an upload it returns
to the sketch after about 10 s. After a sketch that does not use USB, use the
Boot / DOWNLOAD key or a debug probe.
